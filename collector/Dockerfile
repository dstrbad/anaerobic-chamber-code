FROM python:3.12-slim

WORKDIR /app

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY collector.py config.py templates_server.py ./

ENV PYTHONUNBUFFERED=1
ENV DATA_DIR=/data

CMD ["python3", "collector.py"]
