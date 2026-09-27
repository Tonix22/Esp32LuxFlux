ARG IDF_VERSION=v5.5.4
FROM espressif/idf:${IDF_VERSION}

WORKDIR /project
ENV IDF_TARGET=esp32c3
CMD ["bash"]
