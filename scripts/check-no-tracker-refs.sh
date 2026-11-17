#!/usr/bin/env bash
# Fail if added diff lines or commit messages in BASE..HEAD refer to tracker
# numbers such as "todo #12", "room #3", "stack #7" or "ashu #9". Those numbers
# mean nothing outside the tracker; write the reason itself instead. Links to
# this repo's own GitHub issues and PRs ("#12" alone) are fine.
#
# usage: scripts/check-no-tracker-refs.sh BASE [HEAD]
set -eu
BASE=${1:?usage: $0 BASE [HEAD]}
HEAD_REF=${2:-HEAD}
PAT='(todo|room|stack|ashu) #[0-9]+'
SELF=scripts/check-no-tracker-refs.sh
RC=0

# Added lines only: old lines that already match are not this change's fault.
# The script is excluded because it has to spell out the pattern.
HITS=$(git diff --unified=0 --no-color "$BASE" "$HEAD_REF" -- . ":!$SELF" \
    | grep -E '^\+' | grep -vE '^\+\+\+ ' | grep -iE "$PAT" || true)
if [ -n "$HITS" ]; then
    echo "::error::added lines mention a tracker number:"
    echo "$HITS"
    RC=1
fi

# Commit messages of the commits this change adds (merge commits are skipped).
for C in $(git rev-list --no-merges "$BASE..$HEAD_REF"); do
    if git log -1 --format=%B "$C" | grep -iEq "$PAT"; then
        echo "::error::commit $C mentions a tracker number:"
        git log -1 --format=%B "$C" | grep -iE "$PAT"
        RC=1
    fi
done

[ "$RC" -eq 0 ] && echo "no tracker numbers in added lines or commit messages"
exit "$RC"
