Whenever yall start the entire project.
Use the terminal and do this from now onwards:
1. docker-compose up -d --build ml_worker           // For database redis and background ml update pipeline
2. uvicorn app.main:app --reload                    // for fastapi
3. some node command to run the frontend            // @Rujul, do this


During debugging:
1. docker logs -f ecoroute_ml_worker                // This lets you look at the logs for the ML background pipeline.