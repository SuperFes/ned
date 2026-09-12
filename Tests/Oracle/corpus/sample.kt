enum class Colour {
    RED,
    GREEN,
}

class Widget(private val name: String) {
    init {
        require(name.isNotEmpty())
    }

    fun shout(): String {
        return name.uppercase()
    }

    fun describe(n: Int): String {
        return when (n) {
            0 -> "none"
            else -> "some"
        }
    }
}

fun total(values: List<Int>): Int {
    var sum = 0
    values.forEach {
        sum += it
    }
    try {
        return sum
    } catch (e: Exception) {
        return 0
    } finally {
        println("done")
    }
}
