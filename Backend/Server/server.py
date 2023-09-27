# Write a simple http post endpoint that receives an image.
# the server should us python 3s http server and a HTTPRequestHandler class
# The images is transformed to a descriptor using onnxruntime with the CosPlace model CosPlaceResnet50.onnx
# The descriptor is then used to query the redis database for the 100 closest images.
# The reults are then reranked using the LOFTR implementation of the kornia library
# The Longitude, Latidute, Altitude and the found image are returned to the client.

import base64
from io import BytesIO
import numpy as np
import os
from qdrant_client.http import models
from qdrant_client import QdrantClient
from PIL import Image
import torch
import torchvision.transforms as transforms
from fastapi import FastAPI, Request, HTTPException

qdrant_client = QdrantClient(
    "localhost", port=6333, timeout=60, api_key=os.getenv("QDRANT_API_KEY")
)

model = torch.hub.load(
    "gmberton/cosplace", "get_trained_model", backbone="ResNet50", fc_output_dim=512
)
model.eval()

app = FastAPI()


@app.post("/proccess-image")
async def post_image2(request: Request):
    json_data = await request.json()
    limit = json_data["limit"]
    # offset from data or 0
    offset = json_data.get("offset", 0)

    img = None
    try:
        image_1d = np.frombuffer(base64.b64decode(json_data['image']), dtype=np.float32)
        img = torch.from_numpy(image_1d.reshape((1, 3, 512, 512)))
    except Exception as _:
        return HTTPException(status_code=400, detail="Invalid image")
    
    with torch.inference_mode():
        descriptors: torch.FloatTensor = model(img)

        vector_result = qdrant_client.search(
            collection_name="geo-location",
            query_vector=descriptors[0].numpy(),
            query_filter=models.Filter(
                must=[
                    models.FieldCondition(
                        key="IsActive", match=models.MatchValue(value=True)
                    ),
                ]
            ),
            offset=offset,
            limit=limit,
        )

        return {"result": vector_result}