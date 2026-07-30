variable "REGISTRY" {
  default = "ghcr.io/alanpetry"
}

variable "TAG" {
  default = "latest"
}

group "default" {
  targets = ["server", "wsproxy", "web"]
}

target "_common" {
  context = "."
  platforms = ["linux/amd64", "linux/arm64"]
}

target "server" {
  inherits = ["_common"]
  dockerfile = "docker/server.Dockerfile"
  tags = ["${REGISTRY}/openwyd-server:${TAG}"]
}

target "wsproxy" {
  inherits = ["_common"]
  dockerfile = "docker/wsproxy.Dockerfile"
  tags = ["${REGISTRY}/openwyd-wsproxy:${TAG}"]
}

target "web" {
  inherits = ["_common"]
  dockerfile = "docker/web.Dockerfile"
  tags = ["${REGISTRY}/openwyd-web:${TAG}"]
}

