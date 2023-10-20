export function fixGLTF (gltf: any) {
    var parsedGLTF = gltf;

    if (parsedGLTF.extensions.KHR_techniques_webgl !== undefined) {
        //fix Arrays
        parsedGLTF["extensionsUsed"] = ["CESIUM_RTC"];
        parsedGLTF["extensionsRequired"] = ["CESIUM_RTC"];
        delete parsedGLTF.extensions.KHR_techniques_webgl;
        var arrayLength = parsedGLTF.materials.length;
        for (var i = 0; i < arrayLength; i++) {

            var index = 0;
            var tex_coord = 0;
            if (parsedGLTF.materials[i].extensions.KHR_techniques_webgl.values.u_tex !== undefined) {
                index = parsedGLTF.materials[i].extensions.KHR_techniques_webgl.values.u_tex.index;
                tex_coord = parsedGLTF.materials[i].extensions.KHR_techniques_webgl.values.u_tex.texCoord;
            }

            parsedGLTF.materials[i].pbrMetallicRoughness = {
                "baseColorFactor": [
                    0.0,
                    0.0,
                    0.0,
                    1.0
                ],
                "metallicFactor": 0.0,
                "roughnessFactor": 0.0
            };
            parsedGLTF.materials[i].emissiveTexture = {
                "index": index,
                "tex_coord": tex_coord
            };
            parsedGLTF.materials[i].emissiveFactor = [1.0, 1.0, 1.0];
            delete parsedGLTF.materials[i].extensions;
        }   
    } else {
        //console.log("File already converted");
    }


    return parsedGLTF;
}


