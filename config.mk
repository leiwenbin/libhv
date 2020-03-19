# modules
# include icmp dns ftp smtp
WITH_PROTOCOL=yes

WITH_HTTP=yes
WITH_HTTP_SERVER=yes
WITH_HTTP_CLIENT=yes

# WITH_CONSUL need WITH_HTTP_CLIENT=yes
WITH_CONSUL=yes

# features
# base/hsocket.c: replace gethostbyname with getaddrinfo
ENABLE_IPV6=no
# base/RAII.cpp: Windows MiniDumpWriteDump
ENABLE_WINDUMP=no
# http/http_content.h: QueryParams,MultiPart
USE_MULTIMAP=no

# dependencies
# for http/client
WITH_CURL=no
# for http2
WITH_NGHTTP2=no
# for SSL/TLS
WITH_OPENSSL=no
