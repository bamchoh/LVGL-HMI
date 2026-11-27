# Security

## Reporting a vulnerability

Please do not open a public issue for something that can be exploited. Use GitHub's private reporting instead:
**Security → Report a vulnerability** on this repository (<https://github.com/bamchoh/tsukuraku/security/advisories/new>).

Say what you found, in which version (commit), and how to reproduce it. A project file, a screen JSON or a short
script that shows the problem helps most.

There is no fixed response time: this is a small project. Fixes go into `main`; the advisory is published once a fix is in.

## What to keep in mind when using Tsukuraku

A project is a program, not a picture. It can write values to a PLC, send data to other machines, open ports and run
scripts on its own. Run only projects from people you trust, and look at what a project can do before you run it:

```
hml_runtime <project.json> --capabilities
```

See "A project somebody else made" in the README for what the runtime and the designer do to limit a project, and what
they cannot.

日本語: 悪用できる問題は、公開の issue ではなく、このリポジトリの **Security → Report a vulnerability** から知らせてください。
プロジェクトは絵ではなくプログラムです。信頼できる人のプロジェクトだけを動かし、動かす前に
`hml_runtime <project.json> --capabilities` で、そのプロジェクトができることを確かめてください。
