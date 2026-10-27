FROM python:3.11-slim
WORKDIR /app
COPY requirements-ml.txt ./
RUN pip install --no-cache-dir -r requirements-ml.txt
COPY app/ml_service.py ./app/ml_service.py
COPY app/ml/ ./app/ml/
COPY scripts/ml_pipeline/inference.py ./scripts/ml_pipeline/inference.py
ENV PYTHONUNBUFFERED=1
CMD ["python","-m","app.ml_service"]
