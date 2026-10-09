.PHONY: android android-run android-logs test
android:
	python3 tools/build_android.py build
android-run:
	python3 tools/build_android.py run
android-logs:
	python3 tools/build_android.py logs
test:
	cmake -S . -B build/tests -G Ninja -DCMAKE_BUILD_TYPE=Release
	cmake --build build/tests --parallel 4
	ctest --test-dir build/tests --output-on-failure

.PHONY: ios ios-run
ios:
	python3 tools/build_ios.py build
ios-run:
	python3 tools/build_ios.py run
