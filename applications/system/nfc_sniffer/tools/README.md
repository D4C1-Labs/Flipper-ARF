# NFC sniffer offline tools

```bash
# Interactive: banner, guide, prompt for sniff path
python3 decode.py

# Direct
python3 decode.py /path/to/sniff.txt
```

Stages: load → parse → mfkey64 recovery (progress bar) → timeline + summary.

```bash
python3 decode.py sniff.txt --key FFFFFFFFFFFF   # verify known key
python3 decode.py sniff.txt --no-recover         # annotate only
python3 decode.py sniff.txt --plain              # no banner / machine lines
python3 decode.py sniff.txt --no-banner
```
