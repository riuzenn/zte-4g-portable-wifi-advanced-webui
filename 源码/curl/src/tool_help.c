/***************************************************************************
 *                                  _   _ ____  _
 *  Project                     ___| | | |  _ \| |
 *                             / __| | | | |_) | |
 *                            | (__| |_| |  _ <| |___
 *                             \___|\___/|_| \_\_____|
 *
 * Copyright (C) Daniel Stenberg, <daniel@haxx.se>, et al.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution. The terms
 * are also available at https://curl.se/docs/copyright.html.
 *
 * You may opt to use, copy, modify, merge, publish, distribute and/or sell
 * copies of the Software, and permit persons to whom the Software is
 * furnished to do so, under the terms of the COPYING file.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 * SPDX-License-Identifier: curl
 *
 ***************************************************************************/
#include "tool_setup.h"

#include "tool_help.h"
#include "tool_libinfo.h"
#include "tool_util.h"
#include "tool_version.h"
#include "tool_getparam.h"
#include "tool_cfgable.h"

/*
 * 精简构建：不编译分类/选项帮助表。
 * helptext[]（tool_listhelp.c）不再被引用，
 * 由 -fdata-sections + --gc-sections 在链接时丢弃。
 *
 * 帮助分两档：
 *   curl -h        常用选项（~43个）
 *   curl -h more   全部可用选项（~96个）
 *
 * 以下只列当前编译配置下真实可用的选项。
 *   -v / --trace 仍显示 > < 行，不显示 * 行(内部trace)。
 *   --cert-status wolfSSL 禁了 OCSP，可能是 no-op。
 *   --tlsv1/.0/.1 wolfSSL 禁了旧 TLS，选项在但握手可能失败。
 *   --dump-ca-embed 未启用 CURL_CA_EMBED 时输出为空。
 *   --engine wolfSSL 可能不支持，list 可能返回空。
 *   --tls-earlydata wolfSSL 禁了 earlydata，可能是 no-op。
 *   --range / -C off_t 为 32 位（--disable-largefile）。
 */
void tool_help(const char *category)
{
  if(category && curl_strequal(category, "more")) {
    puts(
      "Usage: curl [options...] <url>\n"
      "\n"
      "HTTP (more):\n"
      "  --data-ascii <d>        alias for --data\n"
      "  --data-raw <d>          POST data, no @file interpretation\n"
      "  --url-query <q>         add query parameter to URL\n"
      "  --location-trusted      send auth to redirect target host\n"
      "  --max-redirs <n>        max number of redirects\n"
      "  --follow                follow redirects per spec (8.16+)\n"
      "  --tr-encoding           request transfer-encoding\n"
      "  --raw                   disable content/transfer decoding\n"
      "  --path-as-is            do not squash .. in URL path\n"
      "  --http0.9               allow HTTP/0.9 responses\n"
      "  -0, --http1.0           use HTTP/1.0\n"
      "  --http1.1               use HTTP/1.1\n"
      "  --expect100-timeout <s> timeout for 100-continue\n"
      "  --ignore-content-length ignore Content-Length\n"
      "  --request-target <path> specify HTTP request target\n"
      "  --post301               do not switch POST to GET after 301\n"
      "  --post302               do not switch POST to GET after 302\n"
      "  --post303               do not switch POST to GET after 303\n"
      "  --etag-compare <file>   load ETag from file for conditional req\n"
      "  --etag-save <file>      save response ETag to file\n"
      "  --proto <protocols>     enable/disable protocols\n"
      "  --proto-default <p>     default protocol for scheme-less URLs\n"
      "  --proto-redir <p>       protocols allowed on redirect\n"
      "\n"
      "Auth (more):\n"
      "  --basic                 force HTTP Basic authentication\n"
      "  --oauth2-bearer <tok>   OAuth 2 Bearer Token\n"
      "\n"
      "Fail & Output (more):\n"
      "  --fail-early            exit on first transfer error\n"
      "  --remote-name-all       use -O for all URLs\n"
      "  --output-dir <dir>      default directory for -o/-O\n"
      "  -N, --no-buffer         disable output buffering\n"
      "  --xattr                 store metadata in file xattr\n"
      "  --remove-on-error       remove output file on errors\n"
      "  --no-clobber            do not overwrite existing files\n"
      "  --skip-existing         skip if local file exists\n"
      "  --out-null             discard response data\n"
      "  -R, --remote-time       set local file time from remote\n"
      "\n"
      "TLS (more):\n"
      "  --capath <dir>          CA certificates directory\n"
      "  --cert-type <type>      cert type (PEM|DER)\n"
      "  --key-type <type>       key type (PEM|DER)\n"
      "  --pass <phrase>         passphrase for --key\n"
      "  --ciphers <list>        TLS 1.2 cipher suites\n"
      "  --tls13-ciphers <list>  TLS 1.3 cipher suites\n"
      "  -1, --tlsv1             TLSv1.0+ (may fail: old TLS disabled)\n"
      "  --tlsv1.0               TLSv1.0+ (may fail)\n"
      "  --tlsv1.1               TLSv1.1+ (may fail)\n"
      "  --tlsv1.2               require TLS 1.2+\n"
      "  --tlsv1.3               require TLS 1.3+\n"
      "  --tls-max <ver>         max TLS version (1.2|1.3)\n"
      "  --pinnedpubkey <hash>   pin server public key (sha256//...)\n"
      "  --cert-status           request OCSP status (may be no-op)\n"
      "  --no-alpn              disable ALPN\n"
      "  --no-sessionid          disable TLS session reuse\n"
      "  --ca-native             load CA certs from OS\n"
      "  --crlfile <file>        certificate revocation list\n"
      "  --curves <list>         EC key exchange curves\n"
      "  --dump-ca-embed         write embedded CA bundle to stdout\n"
      "  --engine <name>         crypto engine (list to enumerate)\n"
      "  --sigalgs <list>        TLS signature algorithms\n"
      "  --ssl-allow-beast       allow TLS BEAST vulnerability\n"
      "  --tls-earlydata         allow TLS 1.3 0-RTT (may be no-op)\n"
      "\n"
      "Network (more):\n"
      "  --connect-to <h:p:h:p>  custom host:port mapping\n"
      "  --interface <name>      bind to network interface\n"
      "  --speed-limit <speed>   stop if slower than this\n"
      "  --speed-time <secs>     time window for --speed-limit\n"
      "  --max-filesize <bytes>  max download size\n"
      "  --tcp-nodelay           set TCP_NODELAY\n"
      "  --tcp-fastopen          use TCP Fast Open\n"
      "  --no-keepalive          disable TCP keepalive\n"
      "  --keepalive <secs>      TCP keepalive interval\n"
      "  --keepalive-cnt <int>   max keepalive probes\n"
      "  --keepalive-time <sec>  idle time before first probe\n"
      "  --haproxy-protocol      send HAProxy PROXY v1 header\n"
      "  --haproxy-clientip <ip> set client IP in HAProxy header\n"
      "  --ip-tos <string>       set IP TOS / Traffic Class\n"
      "  --vlan-priority <p>     set VLAN priority\n"
      "  --mptcp                 enable Multipath TCP\n"
      "  --local-port <range>    local port number range\n"
      "  --happy-eyeballs-timeout-ms <ms>  IPv6-before-IPv4 timeout\n"
      "  -4, --ipv4              resolve to IPv4 only\n"
      "\n"
      "Retry & Parallel (more):\n"
      "  --retry-all-errors      retry on any error\n"
      "  --retry-connrefused     retry on connection refused\n"
      "  -Z, --parallel          perform transfers in parallel\n"
      "  --parallel-immediate    do not wait for multiplexing\n"
      "  --parallel-max <n>      max concurrent transfers\n"
      "  --parallel-max-host <n>  max connections to single host\n"
      "\n"
      "Debug & Config (more):\n"
      "  --trace <file>          write trace to file (no * lines)\n"
      "  --trace-ascii <file>    like --trace, ASCII only\n"
      "  --trace-time            add timestamps to trace/verbose\n"
      "  --trace-ids             add transfer+connection IDs\n"
      "  --trace-config <str>    trace config (e.g. all, data, ssl)\n"
      "  --next                  separator for multiple URL configs\n"
      "  --variable <expr>       define variable (e.g. %name=value)\n"
      "  --stderr <file>         redirect stderr to file\n"
      "  --styled-output         enable bold/color for HTTP headers\n"
      "\n"
      "Other:\n"
      "  -q, --disable           disable .curlrc reading\n"
      "  --disallow-username-in-url  error if URL contains username\n"
      "  --rate <max rate>       max request rate for serial transfers\n"
      "\n"
      "Use \"-h\" for common options");
    return;
  }

  puts(
    "Usage: curl [options...] <url>\n"
    "\n"
    "HTTP:\n"
    "  -X, --request <m>       HTTP method (GET|HEAD|POST|PUT)\n"
    "  -d, --data <d>          HTTP POST data\n"
    "  --data-binary <d>       POST binary data (@file or string)\n"
    "  --data-urlencode <d>    POST URL-encoded data\n"
    "  --json <data>           POST JSON (Content-Type: application/json)\n"
    "  -G, --get               put -d data into URL as query string\n"
    "  -H, --header <h>        add header, e.g. -H \"Authorization: Bearer x\"\n"
    "  -e, --referer <url>     set Referer header\n"
    "  -A, --user-agent <s>    set User-Agent\n"
    "  -i, --include           include response headers in output\n"
    "  -I, --head              HEAD request (fetch headers only)\n"
    "  -T, --upload-file <f>   upload file (PUT)\n"
    "  -L, --location          follow redirects\n"
    "  --compressed            request gzip/deflate\n"
    "  -g, --globoff           disable URL globbing\n"
    "  -w, --write-out <fmt>   format output after transfer, e.g. %{http_code}\n"
    "  --url <url>             specify URL explicitly\n"
    "\n"
    "Auth:\n"
    "  -u, --user <u:p>        HTTP basic auth\n"
    "\n"
    "Fail & Output:\n"
    "  -f, --fail              fail silently on HTTP >=400\n"
    "  --fail-with-body        fail but still output body\n"
    "  -S, --show-error        show errors even with -s\n"
    "  -o, --output <file>     write output to file\n"
    "  -O, --remote-name       name output after remote file\n"
    "  -J, --remote-header-name  take filename from Content-Disposition\n"
    "  --create-dirs           create local dirs for -o path\n"
    "  -D, --dump-header <f>   write response headers to file\n"
    "\n"
    "TLS:\n"
    "  -k, --insecure          disable cert verification\n"
    "  --cacert <file>         CA bundle (default /etc/cacert.pem)\n"
    "  --cert <file>           client certificate (mTLS)\n"
    "  --key <file>            client private key (mTLS)\n"
    "\n"
    "Network:\n"
    "  --resolve <h:p:addr>    custom DNS mapping\n"
    "  --limit-rate <rate>     limit transfer speed (e.g. 100k)\n"
    "  --connect-timeout <s>   connection timeout\n"
    "  --max-time <sec>        total timeout\n"
    "\n"
    "Retry:\n"
    "  --retry <n>             retry on transient errors\n"
    "  --retry-delay <sec>     wait between retries\n"
    "  --retry-max-time <sec>  max total time for retries\n"
    "\n"
    "Debug:\n"
    "  -v, --verbose           show req/resp headers (no internal trace)\n"
    "  -K, --config <file>     read config from file\n"
    "\n"
    "Other:\n"
    "  -s, --silent            suppress progress/error output\n"
    "  -#, --progress-bar      show transfer progress as a bar\n"
    "  -V, --version           show version and features\n"
    "  -h, --help              show this text\n"
    "\n"
    "Use \"-h more\" to list more options");
}

static bool is_debug(void)
{
  const char * const *builtin;
  for(builtin = feature_names; *builtin; ++builtin)
    if(curl_strequal("debug", *builtin))
      return TRUE;
  return FALSE;
}

void tool_version_info(void)
{
  const char * const *builtin;
  if(is_debug())
    curl_mfprintf(tool_stderr, "WARNING: this libcurl is Debug-enabled, "
                  "do not use in production\n\n");

  curl_mprintf(CURL_ID "%s\n", curl_version());
#ifdef CURL_PATCHSTAMP
  curl_mprintf("Release-Date: %s, security patched: %s\n",
               LIBCURL_TIMESTAMP, CURL_PATCHSTAMP);
#else
  curl_mprintf("Release-Date: %s\n", LIBCURL_TIMESTAMP);
#endif
  if(built_in_protos[0]) {
#ifndef CURL_DISABLE_IPFS
    const char *insert = NULL;
    /* we have ipfs and ipns support if libcurl has http support */
    for(builtin = built_in_protos; *builtin; ++builtin) {
      if(insert) {
        /* update insertion so ipfs is printed in alphabetical order */
        if(strcmp(*builtin, "ipfs") < 0)
          insert = *builtin;
        else
          break;
      }
      else if(!strcmp(*builtin, "http")) {
        insert = *builtin;
      }
    }
#endif /* !CURL_DISABLE_IPFS */
    curl_mprintf("Protocols:");
    for(builtin = built_in_protos; *builtin; ++builtin) {
      curl_mprintf(" %s", *builtin);
#ifndef CURL_DISABLE_IPFS
      if(insert && insert == *builtin) {
        curl_mprintf(" ipfs ipns");
        insert = NULL;
      }
#endif /* !CURL_DISABLE_IPFS */
    }
    puts(""); /* newline */
  }
  if(feature_names[0]) {
    const char **feat_ext;
    size_t feat_ext_count = feature_count;
#ifdef CURL_CA_EMBED
    ++feat_ext_count;
#endif
#ifdef CURL_DEBUG_GLOBAL_MEM
    ++feat_ext_count;
#endif
    feat_ext = curlx_malloc(sizeof(*feature_names) * (feat_ext_count + 1));
    if(feat_ext) {
      memcpy((void *)feat_ext, feature_names,
             sizeof(*feature_names) * feature_count);
      feat_ext_count = feature_count;
#ifdef CURL_CA_EMBED
      feat_ext[feat_ext_count++] = "CAcert";
#endif
#ifdef CURL_DEBUG_GLOBAL_MEM
      feat_ext[feat_ext_count++] = "global-mem-debug";
#endif
      feat_ext[feat_ext_count] = NULL;
      qsort((void *)feat_ext, feat_ext_count, sizeof(*feat_ext),
            struplocompare4sort);
      curl_mprintf("Features:");
      for(builtin = feat_ext; *builtin; ++builtin)
        curl_mprintf(" %s", *builtin);
      puts(""); /* newline */
      curlx_free((void *)feat_ext);
    }
  }
  if(strcmp(CURL_VERSION, curlinfo->version)) {
    curl_mprintf("WARNING: curl and libcurl versions do not match. "
                 "Functionality may be affected.\n");
  }
}

void tool_list_engines(void)
{
  puts("Build-time engines:\n  <none>");
}
