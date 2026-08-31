# syn-model

Download and manage the local language model that the AI features on a
SynapseOS desktop use. It fetches, verifies and removes them, and nothing more.

```bash
syn-model list              # what is on offer
syn-model status            # what is installed, and where
syn-model download          # the default, mistral-7b
syn-model download tiny -y  # a specific one, without prompting
syn-model remove
```

## The models

| name | what it is | size |
|---|---|---|
| `mistral-7b` | Mistral 7B Instruct Q4_K_M — the recommended one | ~4.1 GB |
| `phi3` | Phi-3 Mini 4K Instruct Q4 — noticeably weaker | ~2.2 GB |
| `tiny` | Qwen2 0.5B Q4 — fits anywhere, and answers like it | ~400 MB |

**A smaller model is not just a smaller download.** Everything downstream —
a natural-language shell, a coding assistant, a desktop AI panel — follows
instructions worse with one, and the failure looks like the tool being broken
rather than the model being small.

## Downloading without a root shell

`syn-model fetch TOKEN` runs a queued download as root, driven by
`syn-model-download@TOKEN.service` and authorised through a polkit rule. That
is how a desktop's downloader asks for a model without any part of the UI
running privileged.

## Notes

Models are large and the download is resumable; `status` is the way to see
what actually landed rather than assuming a finished command means a finished
file.
