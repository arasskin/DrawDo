#!/usr/bin/env bb

(require '[babashka.process :refer [shell]]
         '[babashka.fs :as fs]
         '[cheshire.core :as json]
         '[clojure.string :as str])

(def config
  {:app-name "JunsTodos"
   :bundle-id "com.example.junstodos"
   :sim-build-dir "build-sim"
   :device-build-dir "build-device"
   ;; Apple Team ID for device code signing; the DEVELOPMENT_TEAM
   ;; environment variable overrides it.
   :development-team (or (System/getenv "DEVELOPMENT_TEAM")
                         "466U33S75F")})

;; ---------------------------------------------------------------- shaders

(defn shaders-stale? []
  (let [src (fs/file "src/shaders.glsl")
        out (fs/file "src/shaders.h")]
    (or (not (fs/exists? out))
        (pos? (compare (fs/last-modified-time src)
                       (fs/last-modified-time out))))))

(defn compile-shaders []
  (if (shaders-stale?)
    (do (println "Compiling shaders...")
        (shell "./sokol-shdc -i src/shaders.glsl -o src/shaders.h -l metal_sim:metal_ios"))
    (println "Shaders up to date.")))

;; ---------------------------------------------------------------- cmake

(defn configure [build-dir args]
  (when-not (fs/exists? (fs/file build-dir "CMakeCache.txt"))
    (println (str "Configuring " build-dir "..."))
    (apply shell "cmake" "-B" build-dir
           (concat ["-DCMAKE_SYSTEM_NAME=iOS"
                    "-DCMAKE_OSX_ARCHITECTURES=arm64"
                    "-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0"
                    "-DIOS=ON"]
                   args))))

(defn configure-sim []
  (configure (:sim-build-dir config)
             ["-G" "Ninja"
              "-DCMAKE_BUILD_TYPE=Debug"
              "-DCMAKE_OSX_SYSROOT=iphonesimulator"
              "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"])
  ;; keep compile_commands.json visible to clangd at the project root
  (let [link (fs/path "compile_commands.json")]
    (when-not (fs/exists? link {:nofollow-links true})
      (fs/create-sym-link link (fs/path (:sim-build-dir config) "compile_commands.json")))))

(defn configure-device []
  (when (str/blank? (:development-team config))
    (throw (ex-info "Device builds need a team ID: DEVELOPMENT_TEAM=XXXXXXXXXX bb build.clj --device" {})))
  (configure (:device-build-dir config)
             ["-G" "Xcode"
              "-DCMAKE_OSX_SYSROOT=iphoneos"
              (str "-DDEVELOPMENT_TEAM=" (:development-team config))]))

(defn build-sim []
  (println "Building (simulator)...")
  (shell "cmake --build" (:sim-build-dir config)))

(defn build-device []
  (println "Building (device)...")
  (shell "cmake" "--build" (:device-build-dir config) "--config" "Debug"
         "--" "-allowProvisioningUpdates"))

(defn app-bundle [device?]
  (if device?
    (str (:device-build-dir config) "/Debug-iphoneos/" (:app-name config) ".app")
    (str (:sim-build-dir config) "/" (:app-name config) ".app")))

;; ---------------------------------------------------------------- simulator

(defn find-simulator []
  (let [devices (:out (shell {:out :string} "xcrun simctl list devices"))
        booted (first (filter #(str/includes? % "Booted") (str/split-lines devices)))]
    (if booted
      (second (re-find #"\(([A-Z0-9-]+)\)" booted))
      (let [iphone (first (filter #(str/includes? % "iPhone") (str/split-lines devices)))
            device-id (second (re-find #"\(([A-Z0-9-]+)\)" iphone))]
        (when device-id
          (println "Booting simulator...")
          (shell "xcrun simctl boot" device-id)
          (shell "xcrun simctl bootstatus" device-id))
        device-id))))

(defn run-on-simulator []
  (let [device-id (find-simulator)
        bundle (app-bundle false)]
    (when-not device-id
      (throw (ex-info "No simulator found" {})))
    (println "Installing on simulator...")
    (shell {:continue true} "xcrun simctl uninstall" device-id (:bundle-id config))
    (shell "xcrun simctl install" device-id bundle)
    (println "Launching app...")
    (shell "xcrun simctl launch --console" device-id (:bundle-id config))))

;; ---------------------------------------------------------------- device

(defn find-physical-device []
  (let [f (str (fs/create-temp-file {:suffix ".json"}))]
    (shell "xcrun devicectl list devices --json-output" f)
    (let [devices (get-in (json/parse-string (slurp f) true) [:result :devices])
          connected (filter #(= "connected" (get-in % [:connectionProperties :tunnelState]))
                            devices)]
      (:identifier (first (or (seq connected) (seq devices)))))))

(defn run-on-device []
  (let [device-id (find-physical-device)
        bundle (app-bundle true)]
    (when-not device-id
      (throw (ex-info "No physical device found (is it plugged in and trusted?)" {})))
    (println "Installing on device...")
    (shell "xcrun devicectl device install app --device" device-id bundle)
    (println "Launching app...")
    (shell "xcrun devicectl device process launch --terminate-existing --device"
           device-id (:bundle-id config))))

;; ---------------------------------------------------------------- main

(defn -main [& args]
  (let [device? (some #{"--device"} args)
        no-run? (some #{"--no-run"} args)]
    (try
      (compile-shaders)
      (if device?
        (do (configure-device) (build-device))
        (do (configure-sim) (build-sim)))
      (when-not no-run?
        (if device? (run-on-device) (run-on-simulator)))
      (println "✓ Done!")
      (catch Exception e
        (println "Error:" (.getMessage e))
        (System/exit 1)))))

(apply -main *command-line-args*)
