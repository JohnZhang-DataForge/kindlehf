# Contributing to KindleHF

Thank you for your interest in contributing to KindleHF! Contributions are welcome, including bug fixes, compatibility improvements, tests, documentation, and usability improvements.

The goal is to keep the project understandable, reproducible, legally responsible, and reliable on real Kindle hardware.

## 1. Before you start

- Read the [README](README.md) to understand the project architecture, build process, and current limitations.
- Read [legal.md](legal.md) before submitting code or discussing ROMs, firmware, third-party components, or distribution.
- For a substantial change, open an issue first so the proposed approach and scope can be discussed.
- Keep pull requests focused. Smaller, reviewable changes are easier to verify and maintain.

## 2. AI-assisted code is allowed — understanding is required

You may use AI coding assistants, code generators, autocomplete, and other development tools. **AI-generated code is welcome; blindly submitting code you do not understand is not.**

By submitting a contribution, you are taking responsibility for the code, regardless of who or what produced it. You must be able to:

- Explain what the code does and why the implementation was chosen.
- Walk a reviewer through the important functions, control flow, data structures, and dependencies.
- Explain relevant assumptions, edge cases, failure modes, and security implications.
- Debug the code, interpret test failures, and make follow-up changes without relying on an AI tool to guess.
- Confirm that the code does not contain fabricated APIs, placeholder logic, dead code, unexplained workarounds, or behavior you cannot verify.
- Confirm that generated or copied material has a known origin and is compatible with this repository's licenses.

Treat AI output exactly like code you wrote yourself: review it, test it, and stand behind it. An AI-generated explanation or a passing build is not a substitute for understanding the implementation.

If AI made a substantial contribution, briefly disclose that in the pull request description. Mention the tool if useful and, more importantly, describe what you personally reviewed and tested. Ordinary autocomplete does not need to be itemized.

**Review standard:** maintainers may ask you to explain or revise any part of your contribution. If you cannot explain or responsibly maintain a piece of submitted code, do not submit it yet.

## 3. Contribution workflow

1. Fork the repository or create a feature branch from the latest `main`.
2. Make your changes on that branch. Do not work directly on `main`.
3. Keep each change focused and use clear commit messages.
4. Run the relevant tests and build checks before opening a pull request.
5. Open a pull request against `main`, describe the problem and solution, and complete the checklist below.
6. Respond to review feedback and update your branch as needed.

All changes to `main` go through pull requests. Contributors may open, discuss, and review pull requests, but **contributors must not merge their own pull requests**. A repository administrator/maintainer with the required permission makes the final merge decision.

## 4. Build and test

At minimum, run the checks relevant to your change. The project build entry point is:

```bash
bash ./build.sh
```

The GitHub Actions workflow also runs host-side tests for the file picker, NES core, mapper compatibility, ROM compatibility, framebuffer blitting, and the installation script, followed by the KindleHF cross-build and executable checks.

When changing a specific area, run its corresponding test where practical. Examples of the current CI test targets include:

- `tests/test_file_picker.c`
- `tests/test_core.c`
- `tests/test_mapper227.c`
- `tests/test_rom_compat.c`
- `tests/test_framebuffer_blit.c`

Do not claim a test passed unless you actually ran it and observed the result. If you cannot run a check locally, say so in the pull request and rely on the CI result when it becomes available.

### Hardware validation

A successful host test or cross-compilation does not prove that a change works on a physical Kindle.

For changes involving the framebuffer, e-ink refresh, touchscreen, input events, GTK lifecycle, Kindle firmware libraries, installation, or runtime behavior:

- Test on physical hardware when available.
- State the Kindle model and firmware version used.
- Include concise reproduction steps and relevant logs for bugs.
- Clearly mark behavior as **not tested on physical hardware** when it has not been tested.

Do not describe an unverified hardware behavior as confirmed.

## 5. Code and review expectations

- Prefer clear, maintainable C and straightforward control flow.
- Follow the existing architecture and avoid unrelated refactoring in a bug-fix PR.
- Handle errors and resource cleanup deliberately, especially file descriptors, memory, GTK objects, and device resources.
- Avoid introducing unnecessary dependencies or network downloads at runtime.
- Keep build inputs and third-party source revisions pinned where the existing build process requires reproducibility.
- Add or update tests for bug fixes and new behavior.
- Update documentation when user-visible behavior, supported hardware, build steps, or limitations change.
- Do not remove diagnostics or weaken tests merely to make a build pass.
- Do not claim support for a mapper, ROM format, device, or firmware version beyond what has been verified.

## 6. Copyright, ROMs, firmware, and third-party code

This project does not accept commercial game ROMs or other copyrighted game content.

Do not submit, commit, attach, link to, bundle, or redistribute:

- Commercial game ROMs or disk images.
- BIOS files, copyrighted game artwork, or other proprietary game assets.
- Kindle firmware dumps or proprietary device files.
- Material copied from another project without checking its origin and license.

Do not use this project to bypass copy protection or other access controls.

For third-party code:

- Identify the original project and exact source or revision.
- Verify that its license is compatible with the way it is being used here.
- Preserve required copyright notices and license texts.
- Add or update the relevant notice under `THIRD_PARTY_LICENSES/` and update other license documentation when needed.
- Do not assume that AI-generated code is free of third-party licensing obligations; check any code or snippets supplied by tools.

See [legal.md](legal.md), [COPYING](COPYING), and [LICENSE-NOTES.txt](LICENSE-NOTES.txt). If you are unsure whether content can be included, ask before submitting it.

## 7. Pull request checklist

Please include the following in your pull request:

- [ ] I have described the problem and the behavior this change introduces or fixes.
- [ ] I have read the relevant project and legal documentation.
- [ ] I understand the code I am submitting and can explain and defend the implementation.
- [ ] I have identified the origin and license of any third-party code or assets.
- [ ] I have run the relevant tests and build checks, or clearly stated which checks I could not run.
- [ ] I have not included commercial ROMs, BIOS files, proprietary game assets, or firmware dumps.
- [ ] I have updated documentation and tests where appropriate.
- [ ] I have stated whether physical Kindle hardware testing was performed, when relevant.
- [ ] If AI tools made a substantial contribution, I have disclosed that assistance and described my review and validation.

## 8. Be respectful and constructive

Please be respectful in issues, reviews, and discussions. Explain technical concerns with evidence and actionable suggestions. Questions from new contributors are welcome; the expectation is simply that submitted code is understood, responsibly tested, and maintainable.

Thank you for helping improve KindleHF!
