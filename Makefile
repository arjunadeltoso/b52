SHELL=/bin/bash

DOCKER_IMAGE_NAME=b52-load-tester

build-docker-image:
	docker build -t ${DOCKER_IMAGE_NAME} .

run-docker: build-docker-image
	docker run -d \
		--name ${DOCKER_IMAGE_NAME} \
		--restart always \
		${DOCKER_IMAGE_NAME}

attach-to-docker:
	docker exec -u 0 -it `docker ps | grep ${DOCKER_IMAGE_NAME} | awk '{print $$1;}'` /bin/sh

clear-docker:
	docker stop `docker ps | grep ${DOCKER_IMAGE_NAME} | awk '{print $$1;}'` || true
	docker rm `docker ps -a | grep ${DOCKER_IMAGE_NAME} | awk '{print $$1;}'` || true

# Usage: make run-b52 REQS=100 CONC=5
run-b52:
	docker exec -u 0 -it `docker ps | grep ${DOCKER_IMAGE_NAME} | awk '{print $$1;}'` /b52 ${REQS} ${CONC}

valgrind:
	docker exec -u 0 -it `docker ps | grep ${DOCKER_IMAGE_NAME} | awk '{print $$1;}'` valgrind --leak-check=yes /b52-dbg 2

scan-build:
	docker exec -u 0 -it `docker ps | grep ${DOCKER_IMAGE_NAME} | awk '{print $$1;}'` scan-build-6.0 cc -g -O0 src/b52.c -o /b52-dbg -I/usr/include/mysql/ -lmysqlclient -L/usr/lib/x86_64-linux-gnu -lcurl

