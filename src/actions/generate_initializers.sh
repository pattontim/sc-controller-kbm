#!/bin/bash
SOURCE_ROOT=$(dirname "$0")/../..

get_names() {
	while [ -n "$1" ]; do
		grep -h "scc_actions_init_" "$SOURCE_ROOT/$1" \
			| sed 's/void//' \
			| tr -d '{(;)}' \
			| tr '\n' ' '
		echo -n " "
		shift
	done
}

NAMES=$(get_names "$@")
for name in $NAMES; do
	echo "void $name();"
done
echo
echo "static inline void scc_run_action_initializers() {"
for name in $NAMES; do
	echo "	$name();"
done
echo "}"
