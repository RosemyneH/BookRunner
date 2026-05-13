#!/usr/bin/env bash
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

OLD_TIP="${1:-a0a1ae6}"
export GIT_AUTHOR_NAME="Qt" GIT_AUTHOR_EMAIL="blinkenmage+wow@gmail.com"
export GIT_COMMITTER_NAME="Qt" GIT_COMMITTER_EMAIL="blinkenmage+wow@gmail.com"

git checkout master 2>/dev/null || true
git branch -D master-new 2>/dev/null || true
git checkout --orphan master-new
git reset --hard

commit_one() {
  local old=$1
  git rm -rf . 2>/dev/null || true
  git checkout "$old" -- .
  git add -A
  local msg author_date committer_date
  msg=$(git log -1 --format=%B "$old" | sed '/^Co-authored-by: Cursor <cursoragent@cursor.com>$/d')
  author_date=$(git log -1 --format=%aI "$old")
  committer_date=$(git log -1 --format=%cI "$old")
  GIT_AUTHOR_DATE="$author_date" GIT_COMMITTER_DATE="$committer_date" \
    git -c commit.gpgsign=false commit -m "$msg"
}

commit_one a3e5a30
git rev-list --reverse "a3e5a30..${OLD_TIP}" | while read -r old; do
  commit_one "$old"
done

echo "Rebuilt $(git rev-list --count HEAD) commits"
