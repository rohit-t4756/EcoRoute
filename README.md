# EcoRoute development

## Start the entire application

From the repository root, run:

```bash
docker compose up --build
```

This starts PostGIS, Redis, the FastAPI API, the ML worker, and the React/Vite frontend.

- Frontend: http://localhost:5173
- API: http://localhost:8000
- API docs: http://localhost:8000/docs

The frontend and API use development mounts, so source changes are picked up automatically.

## Stop the application

```bash
docker compose down
```

Add `-v` only when you intentionally want to delete the Postgres and Redis volumes.

## Useful logs

```bash
docker compose logs -f ml_worker
docker compose logs -f api
docker compose logs -f frontend
```
