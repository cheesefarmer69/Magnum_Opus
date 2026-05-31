FROM python:3.11-slim

# alsa-utils levert 'aplay' voor het afspelen via de Pi-audiojack
RUN apt-get update && apt-get install -y --no-install-recommends alsa-utils \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY player.py .

# Audiobestanden worden als volume gemount op /app/audio (zie deploy-audio.sh)
CMD ["python", "-u", "player.py"]
