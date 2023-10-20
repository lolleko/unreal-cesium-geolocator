import path from "path";
import { Worker } from "worker_threads";
import { parseArgs } from "node:util";

import {
  isCompletionMessage,
  isQueueURLMessage,
  Message,
  MessageType,
} from "./messages.js";
import { assert } from "console";
import got from "got";

//Create new worker
const urlQueue = new Set<string>();
const freeWorkers = new Set<Worker>();

let bytesWritten = 0;

let {
  values: { tilesetUrl, assetId, cdns, baseRegex, accessToken, fixGLTF },
} = parseArgs({
  options: {
    tilesetUrl: {
      type: "string",
      short: "t",
    },
    assetId: {
      type: "string",
    },
    cdns: {
      type: "string",
      multiple: true,
    },
    baseRegex: {
      type: "string",
      default: "https:\/\/(a|b|c|d).3d.blc.shc.eu\/WAB\/base_layer\/cesium_mesh_2020\/",
    },
    accessToken: {
      type: "string",
    },
    fixGLTF: {
      type: "boolean",
      default: false,
    }
  },
  strict: true,
});

// const cdns = [
//   "a.3d.blc.shc.eu",
//   "b.3d.blc.shc.eu",
//   "c.3d.blc.shc.eu",
//   "d.3d.blc.shc.eu",
// ];

assert(tilesetUrl || assetId, "tilesetUrl is required");
assert(cdns, "cdns is required");
assert(baseRegex, "baseRegex is required");
assert(accessToken, "accessToken is required");

//const tilesetUrl = 
  // "https://a.3d.blc.shc.eu/WAB/base_layer/cesium_mesh_2020/tileset.json"; // https://assets.ion.cesium.com/1415196/tileset.json?v=1


const authorization = await got(`https://api.cesium.com/v1/assets/${assetId}/endpoint?access_token=${accessToken}`).json() as { accessToken: string, url: string };

accessToken = authorization.accessToken;
tilesetUrl = authorization.url;

for (const cdn of cdns!) {
  const worker = new Worker(new URL("./worker.js", import.meta.url));
  worker.on("message", (message: Message) => {
    if (isQueueURLMessage(message)) {
      if (!urlQueue.has(message.url)) {
        urlQueue.add(message.url);
      }

      if (freeWorkers.size > 0) {
        const worker = freeWorkers.values().next().value;
        if (urlQueue.size > 0) {
          const url = urlQueue.values().next().value;
          freeWorkers.delete(worker);
          urlQueue.delete(url);
          worker.postMessage({ type: MessageType.ScrapeURL, url });
        }
      }
      return;
    }

    if (isCompletionMessage(message)) {
      bytesWritten = bytesWritten + message.bytesWritten;

      if (message.wasFileCreated) {
        console.log(
            `${(bytesWritten / (1024 * 1024 * 1024)).toFixed(
              2
            )} GBs Total; Written ${message.filePath}`
          );
      } else {
        console.log(
            `${(bytesWritten / (1024 * 1024 * 1024)).toFixed(
              2
            )} GBs Total; Already exists: ${message.filePath}`
          );
      }

      if (urlQueue.size > 0) {
        const url = urlQueue.values().next().value;
        urlQueue.delete(url);
        freeWorkers.delete(worker);
        worker.postMessage({ type: MessageType.ScrapeURL, url });
      } else {
        freeWorkers.add(worker);
      }

      return;
    }
  });

  worker.on("error", (error) => {
    console.log(error);
  });

  worker.postMessage({ type: MessageType.Init, cdn: cdn, baseRegex: new RegExp(baseRegex!), accessToken: accessToken!, fixGLTF: fixGLTF });

  freeWorkers.add(worker);
}

const freeWorker = freeWorkers.values().next().value;
freeWorkers.delete(freeWorker);

freeWorker.postMessage({ type: MessageType.ScrapeURL, url: tilesetUrl });
