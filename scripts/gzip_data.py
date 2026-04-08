import os
import gzip
import shutil

SRC = "data_src"
DST = "data"

os.makedirs(DST, exist_ok=True)

for root, _, files in os.walk(SRC):
    for f in files:
        src_path = os.path.join(root, f)
        rel = os.path.relpath(src_path, SRC)
        dst_path = os.path.join(DST, rel + ".gz")

        os.makedirs(os.path.dirname(dst_path), exist_ok=True)

        with open(src_path, 'rb') as fin, gzip.open(dst_path, 'wb') as fout:
            shutil.copyfileobj(fin, fout)

        print("gz:", rel)