"""NPU Vision — VIPLite inference wrapper for face/object detection."""

import asyncio
import json
import logging
import os
import subprocess
from dataclasses import dataclass, field
from typing import List, Optional

logger = logging.getLogger(__name__)

DEFAULT_NPU_BIN = os.path.expanduser("~/ai-sdk/examples/vpm_run/vpm_run")
DEFAULT_NPU_LIB = os.path.expanduser("~/ai-sdk/viplite-tina/lib/aarch64-none-linux-gnu/v2.0")

# ── Data models ──────────────────────────────────────────────

@dataclass
class DetectedFace:
    x: float          # center x (0..1)
    y: float          # center y (0..1)
    width: float      # 0..1
    height: float     # 0..1
    confidence: float # 0..1

@dataclass
class DetectedObject:
    label: str
    confidence: float
    x: float; y: float; width: float; height: float

@dataclass
class VisionResult:
    faces: List[DetectedFace] = field(default_factory=list)
    objects: List[DetectedObject] = field(default_factory=list)
    fps: float = 0.0
    has_face: bool = False
    face_center_x: float = 0.5   # normalized, 0=left, 1=right
    face_center_y: float = 0.5

EMPTY_RESULT = VisionResult()


# ── NPU Runner ───────────────────────────────────────────────

class NPUVision:
    """Runs YOLO/face detection on Allwinner A733 NPU via VIPLite/vpm_run."""

    def __init__(
        self,
        npu_bin: str = DEFAULT_NPU_BIN,
        npu_lib: str = DEFAULT_NPU_LIB,
        model_path: Optional[str] = None,
        input_path: Optional[str] = None,
        enabled: bool = True,
    ):
        self.npu_bin = npu_bin
        self.npu_lib = npu_lib
        self.model_path = model_path or os.path.expanduser(
            "~/ai-sdk/examples/yolov5/model/v3/yolov5.nb"
        )
        self.input_path = input_path or "/tmp/npu_frame.dat"
        self.enabled = enabled and os.path.exists(npu_bin)
        self._last_result = EMPTY_RESULT

        if not self.enabled:
            logger.warning("NPU binary not found at %s — running in SIMULATION mode", npu_bin)

    async def detect(self, frame_data: Optional[bytes] = None) -> VisionResult:
        """Run NPU inference on current frame. Returns detected faces/objects."""
        if not self.enabled:
            return self._simulate()

        # Write frame data
        if frame_data:
            import aiofiles
            async with aiofiles.open(self.input_path, "wb") as f:
                await f.write(frame_data)

        # Run vpm_run
        env = os.environ.copy()
        env["LD_LIBRARY_PATH"] = f"{DEFAULT_NPU_LIB}:{env.get('LD_LIBRARY_PATH', '')}"

        try:
            proc = await asyncio.create_subprocess_exec(
                self.npu_bin, "-s", "/tmp/npu_sample.txt", "-l", "1",
                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                env=env, cwd=os.path.dirname(self.npu_bin),
            )
            stdout, stderr = await asyncio.wait_for(proc.communicate(), timeout=10)

            output = stdout.decode() + stderr.decode()

            # Parse inference time
            fps = 30.0
            import re
            m = re.search(r"profile inference time=(\d+)us", output)
            if m:
                us = int(m.group(1))
                fps = 1_000_000.0 / max(us, 1)

            # Parse detection results (simplified — real parsing depends on model output format)
            result = self._parse_npu_output(output, fps)
            self._last_result = result
            return result

        except Exception as e:
            logger.error("NPU inference error: %s", e)
            return self._simulate()

    def _parse_npu_output(self, output: str, fps: float) -> VisionResult:
        """Parse vpm_run output for detection results."""
        faces = []
        objects = []

        # Simplified parsing — looks for known patterns
        # Real implementation would parse the binary output file
        for line in output.split("\n"):
            line = line.strip().lower()
            if "face" in line and "detect" in line:
                try:
                    parts = line.split()
                    for i, p in enumerate(parts):
                        if "conf" in p and i + 1 < len(parts):
                            conf = float(parts[i + 1].replace(",", ""))
                            if conf > 0.5:
                                faces.append(DetectedFace(x=0.5, y=0.5, width=0.3, height=0.3, confidence=conf))
                except (ValueError, IndexError):
                    pass

        has_face = len(faces) > 0
        return VisionResult(
            faces=faces,
            objects=objects,
            fps=fps,
            has_face=has_face,
            face_center_x=faces[0].x if faces else 0.5,
            face_center_y=faces[0].y if faces else 0.5,
        )

    def _simulate(self) -> VisionResult:
        """Return simulated detection for testing without NPU."""
        import math, time
        # Simulate a wandering face
        t = time.time()
        cx = 0.5 + 0.3 * math.sin(t * 0.5)
        cy = 0.5 + 0.2 * math.sin(t * 0.3 + 1.0)

        return VisionResult(
            faces=[DetectedFace(x=cx, y=cy, width=0.25, height=0.3, confidence=0.92)],
            fps=30.0,
            has_face=True,
            face_center_x=cx,
            face_center_y=cy,
        )

    @property
    def last_result(self) -> VisionResult:
        return self._last_result
