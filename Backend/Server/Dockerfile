# based on python image
FROM python:3.11-slim-bookworm
RUN pip3 install --no-cache-dir numpy fastapi uvicorn Pillow qdrant-client
RUN pip3 install --no-cache-dir torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cpu
COPY ./server.py /server.py
CMD ["uvicorn", "server:app", "--host", "0.0.0.0", "--port", "9000"]
