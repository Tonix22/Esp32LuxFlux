idf_component_register(SRCS "socket_client.cpp" "socket_server.cpp" "udp_socket.cpp"
    INCLUDE_DIRS "include" REQUIRES config lwip esp_common log)
