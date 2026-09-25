# Dark Factory — WeAreDevelopers hackathon

This is the kickoff package for the [WeAreDevelopers hackathon on lablab.ai](https://lablab.ai/ai-hackathons/wearedevelopers-hackathon).
You build a software factory in Band Desktop — at least three coding-agent seats that
plan, implement and review each other's work — and have it build a service, one stage at
a time.

**Read the [participant guide](docs/participant-guide.md) before anything else.** It is
the authoritative source for the rules, schedule, gates, rubric and submission steps.
This README only tells you where things are and how to get going.

## What's in this repository

| Path | What it is |
|---|---|
| [`docs/participant-guide.md`](docs/participant-guide.md) | Rules, schedule, rubric and step-by-step instructions |
| `tablekeeper/` | Track 1: a restaurant reservation system. `spec/stage-1.md`…`stage-4.md` and part of each stage's tests |
| `pocketful/` | Track 2: a wallet and payments app. Same layout as `tablekeeper/` |
| `toy/` | Unscored practice track: a shared counter, with its full test suite and sample mandates |
| `scaffold/` | Minimal Python starting service, used only by the toy walkthrough |
| `harness/` | The `python -m harness` CLI that builds your stage folders and runs the checks |

Pick one track and stay in it. The result you submit is a **separate** repository your
band builds; nothing you submit goes into this one.

## Getting started

You need Python 3.12+, Git, a running Docker daemon, a Band Desktop account and your own
model-provider access. From the root of this checkout:

```sh
python -m venv .venv
. .venv/bin/activate
python -m pip install -r harness/requirements.txt
python -m playwright install chromium
python -m harness --help
```

Then:

1. **Practice on the toy first.** Set up three seats, run the toy through the whole loop
   and do a practice submission check. See
   [Practice on the toy example](docs/participant-guide.md#practice-on-the-toy-example).
2. **Build your track** in a fresh room and a fresh result repository, feeding the band
   one stage's spec at a time. See
   [Build and check each stage](docs/participant-guide.md#build-and-check-each-stage).
3. **Check your work** with the harness:

   ```sh
   python -m harness run --track tablekeeper --repo ../band-work/result --stage 1 \
     --out ../band-work/checks/s1-01
   python -m harness check ../band-work/result --track tablekeeper
   ```

   `run` builds a stage folder and runs the shipped checks; `check` validates layout,
   mandates and `room.json` offline. Use `--mode isolated` for your final runs, since
   that is how entries are judged.
4. **Submit** following [Before you submit](docs/participant-guide.md#before-you-submit).

## Rules that most often cost an entry

These are summaries; the guide has the full wording.

- **Code must come out of your Band room.** Code you write by hand does not count.
- **Build to the spec, not the tests.** Only part of each stage's tests ship with this
  package; judging runs the full set. See
  [Do not write to the tests](docs/participant-guide.md#do-not-write-to-the-tests).
- **Mandates must be generic.** A mandate that names track-specific endpoints, fields or
  error codes disqualifies the entry. See
  [Your mandates must be generic](docs/participant-guide.md#your-mandates-must-be-generic).
- **The submitted run is hands-off.** The task you dispatch for each stage is the only
  human input.

## Help

- Band Desktop, seats, permissions and the harness: the
  [BAND Discord](https://discord.com/invite/5YkNXmYfjk).
- Registration, uploads and prizes: the lablab Discord channel.
- Spec ambiguities: ask in the BAND Discord. Answers are shared publicly with every team.

## License

[Apache 2.0](LICENSE).
