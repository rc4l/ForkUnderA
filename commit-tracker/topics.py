#!/usr/bin/env python3
"""
The "with BERTopic" version: cluster the first-1000 commit messages by MEANING
(embeddings) and auto-label each cluster with its top c-TF-IDF keywords.

Contrast with vocab.py (bag-of-words / TF-IDF, no embeddings): that one counts exact
terms; this one groups commits that are *about* the same thing even when the words differ.

Run with the uv venv:  /tmp/bt312/bin/python commit-tracker/topics.py
Env: UZDOOM, ANCHOR, UNTIL.  Writes commit-tracker/bertopic_tags.tsv.
"""
import os, subprocess
from bertopic import BERTopic
from umap import UMAP

UP     = os.environ.get("UZDOOM", "/Users/talhataj/repos/UZDoom")
ANCHOR = os.environ.get("ANCHOR", "ad88cfc5e")
UNTIL  = os.environ.get("UNTIL")
HERE   = os.path.dirname(os.path.abspath(__file__))
OUT    = os.path.join(HERE, "bertopic_tags.tsv")


def target_shas():
    shas = []
    with open(os.path.join(HERE, "coverage.tsv")) as f:
        for i, line in enumerate(f):
            if i < 2:
                continue
            if len(shas) >= 1000:
                break
            shas.append(line.split("\t", 1)[0])
    return set(shas)


def main():
    want = target_shas()
    args = ["git", "-C", UP, "log", "--format=%H\x1f%s"]
    if UNTIL:
        args.append("--until=" + UNTIL)
    args.append(ANCHOR + "..HEAD")
    out = subprocess.run(args, capture_output=True, encoding="utf-8", errors="replace").stdout

    shas, docs = [], []
    for line in out.splitlines():
        if "\x1f" not in line:
            continue
        sha, subj = line.split("\x1f", 1)
        subj = subj.strip().lstrip("- ").strip()
        if sha in want and not subj.startswith("Merge "):   # drop merge-commit boilerplate
            shas.append(sha)
            docs.append(subj)
    print("documents: %d commit messages (merges dropped)" % len(docs))

    # UMAP has a random seed -> fix it so the run is reproducible
    from sklearn.feature_extraction.text import CountVectorizer
    vec = CountVectorizer(stop_words="english", min_df=2)   # clean topic labels, not "the/to/for"
    umap = UMAP(n_neighbors=15, n_components=5, min_dist=0.0, metric="cosine", random_state=42)
    model = BERTopic(umap_model=umap, vectorizer_model=vec, min_topic_size=5, verbose=True)  # finer -> more topics
    topics, _ = model.fit_transform(docs)
    # reassign the -1 outliers to their nearest topic by c-TF-IDF -> far fewer "misc"
    topics = model.reduce_outliers(docs, topics, strategy="c-tf-idf")
    model.update_topics(docs, topics=topics, vectorizer_model=vec)

    info = model.get_topic_info()
    print("\n--- topics discovered (auto-labeled by keywords) ---")
    for _, row in info.iterrows():
        print("  topic %-3d  n=%-4d  %s" % (row["Topic"], row["Count"], row["Name"]))

    with open(OUT, "w") as f:
        f.write("# sha -> bertopic cluster label (top-3 keywords). topic -1 = outlier.\n")
        for sha, t in zip(shas, topics):
            if t == -1:
                label = "misc"
            else:
                label = "_".join(w for w, _ in model.get_topic(t)[:3])
            f.write("%s\t%s\n" % (sha, label))
    print("\nbertopic_tags.tsv written (%d commits, %d topics)" % (len(shas), len(info) - 1))


if __name__ == "__main__":
    main()
