# TCP Web Server

## Overview
This project implements a simple multithreaded TCP web server in C. The server listens on a specified port, accepts HTTP client connections, and serves files from a local document root directory.

Each incoming connection is handled in a separate thread using POSIX threads (pthread), allowing the server to process multiple client requests concurrently.

The server supports basic HTTP request parsing and response generation, including common HTTP status codes and MIME type detection.

## Features
- Handles multiple clients concurrently using threads
- Parses HTTP request lines (METHOD PATH VERSION)
- Supports HTTP/1.0 and HTTP/1.1
- Serves static files from document root (./www)
- Returns appropiate HTTP status codes
  - 200 OK
  - 400 Bad Request
  - 403 Forbidden
  - 404 Not Found
  - 405 Method Not Allowed
  - 505 HTTP Version Not Supported
- Graceful shutdown using SIGINT (Ctrl+C)

## Building the Server
Compile the server using the provided Makefile

Start the server by specifying a port number: ```./server <port>```



