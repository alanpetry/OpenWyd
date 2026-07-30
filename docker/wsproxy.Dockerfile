# syntax=docker/dockerfile:1.7

FROM python:3.13-slim-bookworm

RUN useradd --create-home --uid 10001 openwyd
WORKDIR /opt/openwyd
COPY --chown=openwyd:openwyd webclient/server/wyd_tcp_proxy.py ./wyd_tcp_proxy.py

USER openwyd
EXPOSE 8282

ENTRYPOINT ["python", "-u", "/opt/openwyd/wyd_tcp_proxy.py"]
CMD ["--listen-host", "0.0.0.0", "--listen-port", "8282", "--target-host", "tmsrv", "--target-port", "8281", "--no-client-target"]

