#!/usr/bin/env python3
"""
benchmarks/harness_dashboard.py

Hybrid Synth Interactive Audio A/B Harness & Real-Time Agent Activity Monitor.
Starts a local web server (http://localhost:8080) with:
  1. Live Agent Activity Monitor (watches what Antigravity AI is doing in real-time: thoughts, tool calls, bash commands, file edits).
  2. Filtered Audio A/B Library (only shows version-tagged files like v54f_*, v55f_*, no unversioned clutter).
  3. Live Build Runner & Terminal Streamer (CMake Standalone, VST3, validate, bench_synth).
"""

import http.server
import socketserver
import os
import sys
import subprocess
import json
import re
import urllib.parse
from pathlib import Path

PORT = 8080
BASE_DIR = Path(__file__).resolve().parent.parent
DATA_DIR = BASE_DIR / "benchmarks" / "data"
BUILD_DIR = BASE_DIR / "build"
RENDER_BIN = BUILD_DIR / "render_audio"
BENCH_BIN = BUILD_DIR / "bench_synth"
CONV_ID = "f4ec9bc3-bd99-40bb-b32d-0a8ad4586661"
TRANSCRIPT_PATH = Path(f"/Users/khoimain/.gemini/antigravity/brain/{CONV_ID}/.system_generated/logs/transcript.jsonl")

os.makedirs(DATA_DIR, exist_ok=True)

def get_current_version():
    try:
        version_file = BASE_DIR / "src" / "Version.h"
        if version_file.exists():
            with open(version_file, "r") as f:
                for line in f:
                    if "HYBRID_SYNTH_VERSION_STRING" in line:
                        parts = line.split('"')
                        if len(parts) >= 2:
                            return parts[1].replace("Version ", "v").replace(".", "").strip()
    except Exception:
        pass
    return "v55f"

def get_recent_agent_events(max_events=25):
    events = []
    if not TRANSCRIPT_PATH.exists():
        return events
    try:
        with open(TRANSCRIPT_PATH, "r", encoding="utf-8", errors="replace") as f:
            lines = f.readlines()
            for line in reversed(lines):
                line = line.strip()
                if not line:
                    continue
                try:
                    obj = json.loads(line)
                    t_type = obj.get("type", "")
                    source = obj.get("source", "")
                    
                    if t_type == "USER_INPUT":
                        raw_c = obj.get("content", "")
                        clean_text = re.sub(r"<[^>]+>", "", raw_c).strip()
                        events.append({
                            "type": "USER",
                            "time": obj.get("created_at", "")[11:19],
                            "title": "User Request",
                            "detail": clean_text[:180] + ("..." if len(clean_text) > 180 else ""),
                            "tag": "USER"
                        })
                    elif t_type == "PLANNER_RESPONSE":
                        tool_calls = obj.get("tool_calls", [])
                        if tool_calls:
                            for tc in tool_calls:
                                name = tc.get("name", "tool")
                                args = tc.get("args", {})
                                summary = args.get("toolSummary", "")
                                cmd = args.get("CommandLine", "")
                                path = args.get("Path", "")
                                detail = cmd if cmd else (path if path else json.dumps(args)[:140])
                                events.append({
                                    "type": "TOOL",
                                    "time": obj.get("created_at", "")[11:19],
                                    "title": f"Agent Action: {summary if summary else name}",
                                    "detail": detail.strip(),
                                    "tag": name
                                })
                        thinking = obj.get("thinking", "")
                        if thinking:
                            events.append({
                                "type": "THINK",
                                "time": obj.get("created_at", "")[11:19],
                                "title": "Agent Reasoning",
                                "detail": thinking[:160] + ("..." if len(thinking) > 160 else ""),
                                "tag": "REASON"
                            })
                        content = obj.get("content", "")
                        if content and not tool_calls:
                            events.append({
                                "type": "AI_MSG",
                                "time": obj.get("created_at", "")[11:19],
                                "title": "Agent Response",
                                "detail": content[:180] + ("..." if len(content) > 180 else ""),
                                "tag": "REPLY"
                            })
                    elif t_type == "GENERIC" and "Command output:" in obj.get("content", ""):
                        c = obj.get("content", "")
                        out_part = c.split("Command output:")[-1].strip()
                        if out_part:
                            first_line = out_part.split("\n")[0][:140]
                            events.append({
                                "type": "OUTPUT",
                                "time": obj.get("created_at", "")[11:19],
                                "title": "Execution Result",
                                "detail": first_line,
                                "tag": "RESULT"
                            })
                except Exception:
                    pass
                if len(events) >= max_events:
                    break
    except Exception as e:
        events.append({"type": "ERR", "time": "", "title": "Log Read Error", "detail": str(e), "tag": "ERR"})
    return events

HTML_TEMPLATE = """<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Hybrid Synth - AI Agent Monitor & Audio A/B Harness</title>
    <style>
        :root {
            --bg: #0d1117;
            --panel: #161b22;
            --panel-border: #30363d;
            --accent: #58a6ff;
            --accent-hover: #1f6feb;
            --success: #238636;
            --warn: #d29922;
            --purple: #8957e5;
            --text: #c9d1d9;
            --text-white: #f0f6fc;
            --text-dim: #8b949e;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
            background: var(--bg);
            color: var(--text);
            padding: 24px;
            max-width: 1240px;
            margin: 0 auto;
        }
        header { display: flex; align-items: center; justify-content: space-between; margin-bottom: 20px; border-bottom: 1px solid var(--panel-border); padding-bottom: 14px; }
        h1 { font-size: 22px; color: var(--text-white); display: flex; align-items: center; gap: 10px; }
        .tag { font-size: 11px; background: #1f6feb; color: white; padding: 3px 8px; border-radius: 12px; font-weight: 600; }
        .live-pulse {
            display: inline-flex; align-items: center; gap: 6px; font-size: 12px; font-weight: 700; color: #3fb950;
        }
        .pulse-dot { width: 8px; height: 8px; border-radius: 50%; background: #3fb950; box-shadow: 0 0 8px #3fb950; animation: blink 1.4s infinite; }
        @keyframes blink { 0%, 100% { opacity: 1; } 50% { opacity: 0.3; } }

        .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 20px; margin-bottom: 24px; }
        @media(max-width: 960px) { .grid { grid-template-columns: 1fr; } }

        .card {
            background: var(--panel);
            border: 1px solid var(--panel-border);
            border-radius: 8px;
            padding: 18px;
        }
        .card h2 { font-size: 15px; color: var(--text-white); margin-bottom: 14px; border-bottom: 1px solid var(--panel-border); padding-bottom: 8px; display: flex; align-items: center; justify-content: space-between; }

        /* Timeline / Live Agent Monitor */
        .timeline {
            max-height: 380px;
            overflow-y: auto;
            display: flex;
            flex-direction: column;
            gap: 8px;
            padding-right: 4px;
        }
        .timeline-item {
            background: #0d1117;
            border-left: 3px solid #30363d;
            border-radius: 0 6px 6px 0;
            padding: 8px 12px;
            font-size: 12px;
            display: flex;
            flex-direction: column;
            gap: 2px;
        }
        .item-tool { border-left-color: #58a6ff; }
        .item-user { border-left-color: #238636; background: rgba(35, 134, 54, 0.08); }
        .item-think { border-left-color: #8957e5; }
        .item-output { border-left-color: #d29922; }

        .item-header { display: flex; justify-content: space-between; align-items: center; }
        .item-title { font-weight: 600; color: var(--text-white); font-size: 12px; }
        .item-time { font-size: 10px; color: var(--text-dim); }
        .item-badge { font-size: 9px; padding: 1px 5px; border-radius: 4px; text-transform: uppercase; font-weight: bold; }
        .badge-tool { background: #1f6feb; color: white; }
        .badge-user { background: #238636; color: white; }
        .badge-think { background: #6e40c9; color: white; }
        .badge-output { background: #9e6a03; color: white; }
        .item-detail { font-family: "JetBrains Mono", Menlo, Consolas, monospace; font-size: 11px; color: var(--text-dim); word-break: break-all; margin-top: 2px; }

        .btn {
            background: #238636;
            color: white;
            border: 1px solid rgba(240, 246, 252, 0.1);
            padding: 7px 12px;
            border-radius: 6px;
            font-size: 12px;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.15s ease;
            display: inline-flex;
            align-items: center;
            gap: 6px;
        }
        .btn:hover { background: #2ea043; }
        .btn-secondary { background: #21262d; border-color: #30363d; color: var(--text-white); }
        .btn-secondary:hover { background: #30363d; }
        .btn-build { background: #1f6feb; }
        .btn-build:hover { background: #388bfd; }

        .audio-row {
            display: flex;
            align-items: center;
            justify-content: space-between;
            background: #0d1117;
            border: 1px solid var(--panel-border);
            border-radius: 6px;
            padding: 10px 14px;
            margin-bottom: 10px;
        }
        .version-badge {
            font-size: 11px;
            font-weight: 700;
            background: #1f6feb;
            color: white;
            padding: 2px 7px;
            border-radius: 4px;
            margin-right: 8px;
        }
        .audio-title { font-weight: 600; font-size: 13px; color: var(--text-white); display: flex; align-items: center; }
        .audio-desc { font-size: 11px; color: var(--text-dim); margin-top: 3px; }
        audio { height: 32px; outline: none; }

        pre {
            background: #0b0d13;
            border: 1px solid var(--panel-border);
            border-radius: 6px;
            padding: 10px;
            font-family: "JetBrains Mono", Menlo, Consolas, monospace;
            font-size: 11px;
            color: #d1d5db;
            overflow-x: auto;
            max-height: 220px;
            white-space: pre-wrap;
        }
        .controls-row { display: flex; gap: 8px; flex-wrap: wrap; margin-bottom: 12px; }
    </style>
</head>
<body>
    <header>
        <div>
            <h1>
                <span>🤖 Antigravity AI Agent Monitor</span>
                <span class="tag" id="current-version">v55.f</span>
            </h1>
            <div style="font-size: 12px; color: var(--text-dim); margin-top: 4px;">Live Telemetry & Activity Feed · Audio A/B Harness</div>
        </div>
        <div style="display: flex; align-items: center; gap: 14px;">
            <div class="live-pulse"><div class="pulse-dot"></div> MONITOR ACTIVE</div>
            <button class="btn btn-secondary" onclick="loadAll()">🔄 Refresh</button>
        </div>
    </header>

    <div class="grid">
        <!-- Panel 1: Live AI Agent Activity Stream -->
        <div class="card">
            <h2>
                <span>⚡ Live AI Actions & Tool Use</span>
                <span style="font-size: 11px; color: var(--text-dim); font-weight: normal;">Updates every 2s</span>
            </h2>
            <div class="timeline" id="agent-timeline">
                <div style="color: var(--text-dim); font-size: 12px;">Reading live transcript feed...</div>
            </div>
        </div>

        <!-- Panel 2: Live Build & Compiler Controls -->
        <div class="card">
            <h2>
                <span>🛠️ Build & Benchmark Runner</span>
                <span id="build-badge" class="tag" style="background: #238636;">PASS (0)</span>
            </h2>
            <div class="controls-row">
                <button class="btn btn-build" onclick="runBuild('all')">🔨 Build All</button>
                <button class="btn btn-secondary" onclick="runBuild('render')">Build render_audio</button>
                <button class="btn btn-secondary" onclick="runBenchmark()">⚡ Run bench_synth</button>
                <button class="btn btn-secondary" onclick="runBuild('validate')">Run pluginval</button>
            </div>
            <pre id="build-log">Compiler ready. Commands executed will stream live output here.</pre>
        </div>
    </div>

    <!-- Panel 3: Quick DSP Synthesizer Renders -->
    <div class="card" style="margin-bottom: 24px;">
        <h2>⚡ Quick DSP Synthesizer Renders (Version-Tagged)</h2>
        <div class="controls-row">
            <button class="btn" onclick="renderAudio('legato_phrase')">🎸 Render Legato Hammer & Pull (E2-F#2-G#2-F#2-E2)</button>
            <button class="btn" onclick="renderAudio('picked_phrase')">🎯 Render Picked Comparison (Same Melody, Pick Strikes)</button>
            <button class="btn" onclick="renderAudio('strum_steel')">Render Steel Acoustic Strum</button>
            <button class="btn" onclick="renderAudio('palm_mute')">Render Palm Mute Chug</button>
            <button class="btn" onclick="renderAudio('dead_note')">Render Dead Note ("X" Thud)</button>
            <button class="btn" onclick="renderAudio('nylon_chord')">Render Classical Nylon (C)</button>
        </div>
        <div id="render-status" style="font-size: 11px; color: var(--text-dim); margin-top: 4px;">Ready.</div>
    </div>

    <!-- Panel 4: Audio A/B Version Library (Filtered, No Clutter) -->
    <div class="card">
        <h2>
            <span>🎧 Audio A/B Version Library</span>
            <span style="font-size: 11px; font-weight: normal; color: var(--text-dim);">Historical & Unversioned files hidden</span>
        </h2>
        <div id="audio-list">
            Loading versioned audio files...
        </div>
    </div>

    <script>
        function loadTimeline() {
            fetch('/api/agent_events')
                .then(r => r.json())
                .then(events => {
                    const container = document.getElementById('agent-timeline');
                    if (!events || events.length === 0) {
                        container.innerHTML = '<div style="color: #8b949e; font-size: 12px;">No active events found.</div>';
                        return;
                    }
                    container.innerHTML = events.map(e => {
                        let itemClass = "item-tool";
                        let badgeClass = "badge-tool";
                        if (e.type === "USER") { itemClass = "item-user"; badgeClass = "badge-user"; }
                        else if (e.type === "THINK") { itemClass = "item-think"; badgeClass = "badge-think"; }
                        else if (e.type === "OUTPUT") { itemClass = "item-output"; badgeClass = "badge-output"; }

                        return `
                            <div class="timeline-item ${itemClass}">
                                <div class="item-header">
                                    <div class="item-title">
                                        <span class="item-badge ${badgeClass}">${e.tag}</span>
                                        ${e.title}
                                    </div>
                                    <span class="item-time">${e.time}</span>
                                </div>
                                <div class="item-detail">${e.detail}</div>
                            </div>
                        `;
                    }).join('');
                });
        }

        function loadAudioList() {
            fetch('/api/list_audio')
                .then(r => r.json())
                .then(data => {
                    document.getElementById('current-version').innerText = data.current_version;
                    const container = document.getElementById('audio-list');
                    if (data.files.length === 0) {
                        container.innerHTML = '<div style="color: #8b949e; font-size: 12px; padding: 12px 0;">No version-tagged files found yet.</div>';
                        return;
                    }
                    container.innerHTML = data.files.map(f => `
                        <div class="audio-row">
                            <div>
                                <div class="audio-title">
                                    <span class="version-badge">${f.version}</span>
                                    <span>${f.title}</span>
                                </div>
                                <div class="audio-desc">${f.desc} • ${f.size}</div>
                            </div>
                            <audio controls src="/audio/${f.filename}"></audio>
                        </div>
                    `).join('');
                });
        }

        function runBuild(target) {
            const logBox = document.getElementById('build-log');
            logBox.innerText = `Starting build for target [${target}]...\\n`;
            fetch(`/api/build?target=${target}`)
                .then(r => r.json())
                .then(res => { logBox.innerText = res.output; });
        }

        function renderAudio(preset) {
            const status = document.getElementById('render-status');
            status.innerText = `Synthesizing ${preset}... please wait...`;
            fetch(`/api/render?preset=${preset}`)
                .then(r => r.json())
                .then(res => {
                    status.innerText = res.message;
                    loadAudioList();
                });
        }

        function runBenchmark() {
            const logBox = document.getElementById('build-log');
            logBox.innerText = "Running bench_synth real-time latency & multi-track stress test...";
            fetch('/api/benchmark')
                .then(r => r.json())
                .then(res => { logBox.innerText = res.output; });
        }

        function loadAll() {
            loadTimeline();
            loadAudioList();
        }

        loadAll();
        // Live polling every 2.5 seconds
        setInterval(loadTimeline, 2500);
    </script>
</body>
</html>
"""

class HarnessHandler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        query = urllib.parse.parse_qs(parsed.query)

        if path == "/" or path == "/index.html":
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.end_headers()
            self.wfile.write(HTML_TEMPLATE.encode("utf-8"))
            return

        if path.startswith("/audio/"):
            filename = path.replace("/audio/", "")
            file_path = DATA_DIR / filename
            if file_path.is_file():
                self.send_response(200)
                self.send_header("Content-Type", "audio/wav")
                self.send_header("Content-Length", str(file_path.stat().st_size))
                self.end_headers()
                with open(file_path, "rb") as f:
                    self.wfile.write(f.read())
            else:
                self.send_error(404, "File Not Found")
            return

        # Return live agent events from transcript.jsonl
        if path == "/api/agent_events":
            events = get_recent_agent_events(25)
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps(events).encode("utf-8"))
            return

        if path == "/api/list_audio":
            items = []
            ver_regex = re.compile(r"^(v\d+[a-z]?)_(.*)\.wav$", re.IGNORECASE)
            for f in sorted(DATA_DIR.glob("*.wav"), key=lambda p: p.stat().st_mtime, reverse=True):
                match = ver_regex.match(f.name)
                if match:
                    ver_tag = match.group(1).lower()
                    rest_name = match.group(2)
                    size_kb = f.stat().st_size // 1024
                    clean_title = rest_name.replace("_", " ").title()
                    desc = "Acoustic Synthesizer Render"
                    if "legato" in rest_name: desc = "Acoustic Legato (Smooth Hammer-On & Left-Hand Pizzicato Pull-Off, No Pick Snap)"
                    elif "picked" in rest_name: desc = "Picked Comparison (Same Melody E2-F#2-G#2-F#2-E2 with Individual Plectrum Strikes)"
                    elif "palm" in rest_name: desc = "Palm Mute (Viscoelastic flesh boundary damping)"
                    elif "dead" in rest_name: desc = "Dead Note (Zero-pitch mechanical dipole impulse)"
                    elif "strum" in rest_name: desc = "Physical Acoustic Strum (E Major, downward wrist arc)"
                    elif "nylon" in rest_name: desc = "Classical Nylon Fingerstyle (Chord C)"
                    items.append({
                        "version": ver_tag,
                        "title": clean_title,
                        "filename": f.name,
                        "size": f"{size_kb} KB",
                        "desc": desc
                    })
            resp = {"current_version": get_current_version(), "files": items}
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps(resp).encode("utf-8"))
            return

        if path == "/api/build":
            target = query.get("target", ["all"])[0]
            cmd = ["cmake", "--build", str(BUILD_DIR), "--config", "Release"]
            if target == "render": cmd += ["--target", "render_audio"]
            elif target == "bench": cmd += ["--target", "bench_synth"]
            elif target == "validate": cmd += ["--target", "validate"]

            try:
                proc = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
                out = (proc.stdout + "\n" + proc.stderr).strip()
                resp = {"status": "ok", "passed": (proc.returncode == 0), "output": out if out else "Build completed successfully."}
            except Exception as e:
                resp = {"status": "error", "passed": False, "output": str(e)}

            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps(resp).encode("utf-8"))
            return

        if path == "/api/render":
            preset = query.get("preset", ["strum_steel"])[0]
            ver = get_current_version()
            out_file = f"{ver}_custom.wav"
            cmd = []
            if preset == "legato_phrase":
                out_file = f"{ver}_legato_hammer_pull.wav"
                cmd = [str(RENDER_BIN), "--engine", "1", "--material", "1", "--chord", "40,42,44,42,40", "--seq", "0.40", "--dur", "3.5", "--out", str(DATA_DIR / out_file)]
            elif preset == "picked_phrase":
                out_file = f"{ver}_picked_comparison_phrase.wav"
                cmd = [str(RENDER_BIN), "--engine", "1", "--material", "1", "--chord", "40,42,44,42,40", "--seq", "0.40", "--noLegato", "--dur", "3.5", "--out", str(DATA_DIR / out_file)]
            elif preset == "strum_steel":
                out_file = f"{ver}_strum_steel.wav"
                cmd = [str(RENDER_BIN), "--engine", "1", "--material", "1", "--chord", "40,47,52,55,59,64", "--strum", "12", "--dur", "3.0", "--out", str(DATA_DIR / out_file)]
            elif preset == "palm_mute":
                out_file = f"{ver}_palm_mute.wav"
                cmd = [str(RENDER_BIN), "--engine", "1", "--material", "1", "--chord", "40,47,52,55,59,64", "--palm", "--dur", "2.0", "--out", str(DATA_DIR / out_file)]
            elif preset == "dead_note":
                out_file = f"{ver}_dead_note.wav"
                cmd = [str(RENDER_BIN), "--engine", "1", "--material", "1", "--chord", "40,47,52,55,59,64", "--dead", "--dur", "1.5", "--out", str(DATA_DIR / out_file)]
            elif preset == "nylon_chord":
                out_file = f"{ver}_nylon_chord_c.wav"
                cmd = [str(RENDER_BIN), "--engine", "1", "--material", "0", "--chord", "48,52,55,60,64", "--dur", "3.0", "--out", str(DATA_DIR / out_file)]

            try:
                proc = subprocess.run(cmd, capture_output=True, text=True, check=True)
                resp = {"status": "ok", "message": f"Successfully rendered {out_file}", "output": proc.stdout}
            except Exception as e:
                resp = {"status": "error", "message": str(e)}

            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps(resp).encode("utf-8"))
            return

        if path == "/api/benchmark":
            try:
                proc = subprocess.run([str(BENCH_BIN)], capture_output=True, text=True, timeout=20)
                resp = {"status": "ok", "passed": ("ALL TESTS PASSED" in proc.stdout), "output": proc.stdout}
            except Exception as e:
                resp = {"status": "error", "passed": False, "output": str(e)}

            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps(resp).encode("utf-8"))
            return

        self.send_error(404, "Endpoint not found")

def main():
    socketserver.TCPServer.allow_reuse_address = True
    with socketserver.TCPServer(("", PORT), HarnessHandler) as httpd:
        print(f"============================================================")
        print(f"🤖 Antigravity AI Agent Monitor running at http://localhost:{PORT}")
        print(f"============================================================")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nShutting down monitor server.")

if __name__ == "__main__":
    main()
