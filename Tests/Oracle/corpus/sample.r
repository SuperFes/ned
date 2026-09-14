# A representative script: functions, S3 methods, pipes, vectors.
library(dplyr)

summarise_widget <- function(name, counts = c(0)) {
  total <- sum(counts)
  data.frame(name = name, total = total)
}

widgets <- list("a", "b", "c")
results <- widgets |>
  purrr::map(function(w) summarise_widget(w, c(1, 2, 3)))

print.widget_summary <- function(x, ...) {
  cat(sprintf("%s: %d\n", x$name, x$total))
}

for (w in results) {
  if (w$total > 0) {
    print(w)
  } else {
    next
  }
}
