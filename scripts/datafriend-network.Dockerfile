FROM ubuntu:24.04
RUN apt-get update -qq && apt-get install -y -qq --no-install-recommends g++ make cmake liburing-dev libsodium-dev python3 ca-certificates && rm -rf /var/lib/apt/lists/*
