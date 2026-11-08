#!/bin/sh
# gyvm 便捷启动：需要时先构建，然后运行
set -e
cd "$(dirname "$0")"

if [ ! -x build/gyvm ]; then
	cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
	cmake --build build >/dev/null
fi

exec ./build/gyvm "$@"
