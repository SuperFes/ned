(ns shapes.core
  (:require [clojure.string :as str]))

(defn area [{:keys [w h]}]
  (let [w (or w 0)
        h (or h 0)]
    (if (pos? w)
      (* w h)
      0)))

(defn describe [shapes]
  (str/join ", "
            (map area shapes)))

(def config {:width 10
             :height 20})
