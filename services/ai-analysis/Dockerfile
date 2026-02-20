FROM python:3.12-slim

WORKDIR /app

# Install system dependencies (curl for healthcheck)
RUN apt-get update && \
    apt-get install -y --no-install-recommends curl && \
    rm -rf /var/lib/apt/lists/*

# Install Python dependencies
COPY services/ai-analysis/requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# Copy application code
COPY services/ai-analysis/app ./app

# Create non-root user
RUN useradd -r -u 1001 appuser && \
    mkdir -p /app/logs && \
    chown -R appuser:appuser /app
USER appuser

ENV PYTHONUNBUFFERED=1
ENV PYTHONDONTWRITEBYTECODE=1

EXPOSE 8085

CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8085", "--log-level", "info"]
