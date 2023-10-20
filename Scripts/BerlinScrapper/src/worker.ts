import * as path from "path";
import { promises as fs } from "fs";
import extractB3dm from "3d-tiles-tools/lib/extractB3dm.js";
import gltfPipeline from "gltf-pipeline";
import { fixGLTF as fixGLTFV1 } from "./FixGLTF/FixGLTF.js";
import glbToB3dm from "3d-tiles-tools/lib/glbToB3dm.js";
import * as url from "url";
import { got, Response } from "got";

import { parentPort } from "worker_threads";

let cdn: string;

let fixGLTF: boolean;

let baseRegex =
  /https:\/\/(a|b|c|d).3d.blc.shc.eu\/WAB\/base_layer\/cesium_mesh_2020\//;

let accessToken: string = "";

const uriRegex = /"uri":\s*"([^"]+)"/g;

const uriRegexBodyCleaner = /"uri":\s*"([^"]+)\?([^"]+)"/g;

import pLimit from "p-limit";
import {
  CompletionMessage,
  InitMessage,
  isInitMessage,
  isScrapeURLMessage,
  Message,
  MessageType,
  ScrapeURLMessage,
} from "./messages.js";

const limit = pLimit(24);

async function findAndRequestAllURLsInJson(json: string, requestUrl: string) {
  let responseText = json;

  responseText = responseText.replace(uriRegexBodyCleaner, '"uri": "$1"');

  for (const match of responseText.matchAll(uriRegex)) {
    const parsedUrl = url.parse(requestUrl);

    const newRequestUrl = `https://${parsedUrl.hostname}${path.dirname(
      parsedUrl.pathname as string
    )}/${match[1].replaceAll("\\", "")}`;

    parentPort!.postMessage({
      type: MessageType.QueueURL,
      url: newRequestUrl,
    });
  }

}
async function parseJson(response: Response<Buffer>) {
  findAndRequestAllURLsInJson(response.body.toString(), response.url);

  return response.body;
}

async function parseB3DM(response: Response<Buffer>) {
  const buffer = response.body;

  const res = extractB3dm(buffer);

  var gltf = await gltfPipeline.glbToGltf(res.glb);

  if (fixGLTF) {
    try {
      gltf = fixGLTFV1(gltf.gltf);
    } catch (err) {
      console.log(gltf.gltf);
    }
  }


  // https://github.com/CesiumGS/gltf-pipeline/blob/master/lib/compressDracoMeshes.js#L491
  const glb = await gltfPipeline.gltfToGlb(gltf, {
    dracoOptions: {
      compressionLevel: 7,
      quantizePositionBits: 11,
      quantizeNormalBits: 8,
      quantizeTexcoordBits: 10,
      quantizeColorBits: 8,
      quantizeGenericBits: 8
    }
  });
  const b3dm = glbToB3dm(
    glb.glb,
    res.featureTable.json,
    res.featureTable.binary,
    res.batchTable.json,
    res.batchTable.binary
  );

  return b3dm;
}

function init(message: InitMessage) {
    cdn = message.cdn;
    baseRegex = message.baseRegex;
    accessToken = message.accessToken;
    fixGLTF = message.fixGLTF;
    return;
}

async function scrapeURL(message: ScrapeURLMessage) {
    const outDir = "./tileset";

    const urlWithAnyCDN = new URL(message.url);
    urlWithAnyCDN.hostname = cdn;
    const requestUrl = urlWithAnyCDN.href;

    const urlPath = requestUrl.replace(baseRegex, "");

    const filePath = path.join(outDir, urlPath).split("?")[0];

    try {
      // dont redownload b3dm files & only reparse json
      const fileStats = await fs.stat(filePath);

      if (filePath.includes(".json")) {
        const fileBuffer = await fs.readFile(filePath);
        findAndRequestAllURLsInJson(fileBuffer.toString(), requestUrl);
      }

      parentPort!.postMessage({
        type: MessageType.Completion,
        bytesWritten: fileStats.size,
        filePath,
        wasFileCreated: false,
      } as CompletionMessage);

      return;
    } catch (err) {}

    var fileDir = path.dirname(filePath);

    await fs.mkdir(fileDir, { recursive: true });

    let response: Response<Buffer>;
    try {
      response = await limit(() =>
        got(requestUrl, {
          retry: {
            limit: 100,
            methods: ["GET"],
          },
          headers: { "authorization": "Bearer " + accessToken },
          responseType: "buffer",
        })
      );
    } catch (e) {
      console.log("Error downloading: " + requestUrl);
      return;
    }

    let result: Buffer;

    if (response.headers["content-type"] === "application/json") {
      result = await parseJson(response);
    } else {
      result = await parseB3DM(response);
    }

    await fs.writeFile(filePath, result);

    parentPort!.postMessage({
      type: MessageType.Completion,
      bytesWritten: result.length,
      filePath,
      wasFileCreated: true,
    } as CompletionMessage);
}

parentPort!.on("message", async (message: Message) => {
  if (isInitMessage(message)) {
    init(message);
    return;
  }
  if (isScrapeURLMessage(message)) {
    scrapeURL(message);
    return;
  }
});
