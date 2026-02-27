.PHONY: dev prod down

dev: ## Local dev with mkcert TLS (LAN/iPhone testing)
	docker compose --env-file .env.local -f docker-compose.yml -f docker-compose.local.yml up --build

prod: ## Production (Let's Encrypt TLS)
	docker compose up --build

down: ## Stop all containers
	docker compose down
