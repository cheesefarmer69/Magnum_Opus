FROM python:3.11-slim

WORKDIR /app

# Dependencies eerst (betere Docker layer caching)
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# Code daarna
COPY bridge.py .

CMD ["python", "-u", "bridge.py"]