FROM python:3.12-slim

WORKDIR /app

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY app ./app
COPY alembic ./alembic
COPY alembic.ini .
COPY docker-entrypoint.sh .

RUN find . -type f -name "*.sh" -exec sed -i 's/\r$//' {} + \
    && sed -i '1s/^\xEF\xBB\xBF//' docker-entrypoint.sh \
    && chmod +x docker-entrypoint.sh

ENTRYPOINT ["./docker-entrypoint.sh"]
