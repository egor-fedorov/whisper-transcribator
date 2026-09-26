from dataclasses import dataclass
from typing import Any


@dataclass(frozen=True)
class Segment:
    id: int
    start: float
    end: float
    text: str
    avg_logprob: float | None = None
    compression_ratio: float | None = None
    no_speech_prob: float | None = None
    words: list[Any] | None = None


@dataclass(frozen=True)
class TranscriptInfo:
    language: str
    language_probability: float
    duration: float
    duration_after_vad: float
