#!/usr/bin/env python3
"""range_server.py — FR-8 模型管理验收专用本地 HTTP 服务（python3 stdlib，无第三方依赖）。

职责：目录静态服务 + HEAD 支持 + Range 请求回 206（SimpleHTTPRequestHandler 不回 206，
故自写 handler）+ 下载失败注入 + 请求日志。日志是 MDL-RESUME 断言「客户端确实发了
Range: bytes=N-」的 ground truth（客户端只看结果文件无法区分续传与整体重下）。

注入开关：
  --fail 404|429     所有请求（HEAD+GET）恒回该状态码
  --failget 404|429  HEAD 正常、GET 恒回该状态码（GET 段 HTTP 错误路径，区别于 HEAD 段）
  --cutmid N         GET 发 N 字节后掐断连接（Content-Length 照常声明完整剩余量 → 客户端
                     收到 premature close，连接级错误）
  --cutall           GET 只发响应头不发任何字节即掐断
  --no206            无视 Range 头恒回 200 全量（服务器不支持 Range 的兼容路径）
  --chunk-sleep-ms N 每发一个 chunk 睡 N 毫秒（节流：把小文件下载拉慢到秒级，供 GUI 验收
                     在传输期内做 busy/取消断言，避免竞态；0 = 不睡）
  --etag HEX         覆盖 ETag（字面 hex，不含引号）；缺省 = 所服务文件的 sha256（对齐 HF
                     LFS/Xet 契约：权重文件 ETag 即 sha256）；传 none 则不发 ETag 头

用法：
  range_server.py --root DIR [--port 0] [--bind 127.0.0.1] [--log FILE] [注入开关...]
启动完成后 stdout 打一行 "READY <port>"（--port 0 → 实际端口）。
每请求向日志（缺省 stderr）记一行：METHOD path range=<Range 头或-> status=N

红线遵守：仅回环地址监听，不触真网；服务的是测试 fixture（mktemp 或 gitignore 的
third_party 副本），权重不入库。
"""
import argparse
import hashlib
import os
import socket
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

CHUNK = 1 << 16


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "RangeSrv/1.0"

    def log_message(self, fmt, *args):  # 静默缺省访问日志（自定义日志为准）
        pass

    # ---- 工具 ----

    def _abort(self):
        """掐断连接（cutmid/cutall）。必须 shutdown(SHUT_WR)：socket.close() 在 rfile/wfile
        makefile 引用存续时不关 OS fd——实测客户端 8 秒收不到 FIN，掐断失效。"""
        try:
            self.wfile.flush()
        except OSError:
            pass
        try:
            self.connection.shutdown(socket.SHUT_WR)
        except OSError:
            pass
        self.close_connection = True

    def _log(self, status):
        rng = self.headers.get("Range", "-")
        self.server.sink.write(f"{self.command} {self.path} range={rng} status={status}\n")
        self.server.sink.flush()

    def _resolve(self):
        """URL path → root 内文件（剥前导 /；realpath 防越界）；无对应文件回 None。"""
        rel = self.path.split("?", 1)[0].lstrip("/")
        path = os.path.realpath(os.path.join(self.server.root, rel))
        root = os.path.realpath(self.server.root)
        if os.path.commonpath([path, root]) != root:
            return None
        return path if os.path.isfile(path) else None

    def _etag(self, path):
        if self.server.suppress_etag:
            return None
        if self.server.etag:
            return self.server.etag
        key = (path, os.path.getmtime(path), os.path.getsize(path))
        hit = self.server.etag_cache.get(key)
        if hit:
            return hit
        h = hashlib.sha256()
        with open(path, "rb") as f:
            for chunk in iter(lambda: f.read(CHUNK), b""):
                h.update(chunk)
        self.server.etag_cache[key] = h.hexdigest()
        return h.hexdigest()

    # ---- 请求处理 ----

    def _respond(self, send_body):
        srv = self.server
        code = srv.fail or (srv.failget if self.command != "HEAD" else None)
        if code:
            self._log(code)
            self.send_error(code, "injected failure")
            return
        path = self._resolve()
        if path is None:
            self._log(404)
            self.send_error(404, "not found")
            return
        size = os.path.getsize(path)
        etag = self._etag(path)

        start = 0
        end = size - 1
        use206 = bool(self.headers.get("Range")) and not srv.no206
        if use206:
            # 只支持单区间：bytes=N-（客户端形态）/ bytes=N-M（端点含端，HTTP 语义）
            spec = self.headers["Range"].split("=", 1)[1].split(",", 1)[0].strip()
            first, _, last = spec.partition("-")
            start = int(first) if first else 0
            end = min(int(last) if last else size - 1, size - 1)
            if start < 0 or start > end or start >= size:
                self._log(416)
                self.send_response(416)
                self.send_header("Content-Range", f"bytes */{size}")
                self.send_header("Content-Length", "0")
                self.end_headers()
                return

        total = end - start + 1
        status = 206 if use206 else 200
        self._log(status)
        self.send_response(status)
        if etag:
            self.send_header("ETag", f'"{etag}"')
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Accept-Ranges", "bytes")
        if use206:
            self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        self.send_header("Content-Length", str(total))
        self.end_headers()
        if self.command == "HEAD" or not send_body:
            return
        if srv.cutall:
            self._abort()
            return

        remaining = total
        sent = 0
        with open(path, "rb") as f:
            f.seek(start)
            while remaining > 0:
                chunk = f.read(min(CHUNK, remaining))
                if not chunk:
                    break
                if srv.cutmid is not None and sent + len(chunk) > srv.cutmid:
                    take = srv.cutmid - sent
                    if take > 0:
                        self.wfile.write(chunk[:take])
                    self._abort()  # 声明量未发完即断 → 客户端 premature close
                    return
                self.wfile.write(chunk)
                sent += len(chunk)
                remaining -= len(chunk)
                if srv.chunk_sleep_ms:  # 节流开关：慢化传输供 busy/取消类断言（见 docstring）
                    time.sleep(srv.chunk_sleep_ms / 1000.0)

    def do_HEAD(self):
        self._respond(send_body=False)

    def do_GET(self):
        self._respond(send_body=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", required=True, help="服务根目录（URL path 映射其下文件）")
    ap.add_argument("--port", type=int, default=0, help="监听端口（0 = 系统分配）")
    ap.add_argument("--bind", default="127.0.0.1", help="监听地址（缺省仅回环，红线：不触真网）")
    ap.add_argument("--fail", type=int, choices=[404, 429], help="HEAD+GET 恒回该状态码")
    ap.add_argument("--failget", type=int, choices=[404, 429], help="仅 GET 恒回该状态码（HEAD 正常）")
    ap.add_argument("--cutmid", type=int, metavar="N", help="GET 发 N 字节后掐断连接")
    ap.add_argument("--cutall", action="store_true", help="GET 只发响应头即掐断")
    ap.add_argument("--no206", action="store_true", help="无视 Range 恒回 200 全量")
    ap.add_argument("--chunk-sleep-ms", type=int, default=0, metavar="N",
                    help="每发一个 chunk 睡 N 毫秒（节流供 GUI busy/取消断言；0 = 不睡）")
    ap.add_argument("--etag", help="覆盖 ETag（hex 字面值）；none = 不发 ETag 头；缺省 = 文件 sha256")
    ap.add_argument("--log", help="请求日志文件（缺省 stderr）")
    args = ap.parse_args()

    sink = open(args.log, "a", buffering=1) if args.log else sys.stderr

    class Server(ThreadingHTTPServer):
        daemon_threads = True
        request_queue_size = 16

    httpd = Server((args.bind, args.port), Handler)
    httpd.root = os.path.abspath(args.root)
    httpd.fail = args.fail
    httpd.failget = args.failget
    httpd.cutmid = args.cutmid
    httpd.cutall = args.cutall
    httpd.no206 = args.no206
    httpd.chunk_sleep_ms = args.chunk_sleep_ms
    httpd.etag = args.etag if args.etag and args.etag != "none" else None
    httpd.suppress_etag = args.etag == "none"
    httpd.etag_cache = {}
    httpd.sink = sink
    print(f"READY {httpd.server_address[1]}", flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
