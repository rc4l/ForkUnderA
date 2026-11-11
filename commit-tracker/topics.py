#!/usr/bin/env python3
"""
The "with BERTopic" version: cluster the first-1000 commit messages by MEANING
(embeddings) and auto-label each cluster with its top c-TF-IDF keywords.

Contrast with vocab.py (bag-of-words / TF-IDF, no embeddings): that one counts exact
terms; this one groups commits that are *about* the same thing even when the words differ.

Run with a venv that has bertopic:  python commit-tracker/topics.py [--ledger NAME] [--limit N]
Reads titles from the ledger's coverage.tsv.  Writes <ledger>/bertopic_tags.tsv.
"""
import argparse, os, sys
from bertopic import BERTopic
from umap import UMAP

HERE   = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ledger as L  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ledger", default="uzdoom")
    ap.add_argument("--limit", type=int, default=1000)
    args = ap.parse_args()
    led = L.load_config()[args.ledger]
    shas, docs = [], []
    for r in L.read_rows(led)[:args.limit]:
        subj = r["title"].strip().lstrip("- ").strip()
        if not subj.startswith("Merge "):   # drop merge-commit boilerplate
            shas.append(r["sha"])
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

    with open(os.path.join(L.ledger_dir(led), "bertopic_tags.tsv"), "w", encoding="utf-8", newline="\n") as f:
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
