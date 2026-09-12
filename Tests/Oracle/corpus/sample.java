interface Sized {
    int size();
}

enum Colour {
    RED,
    GREEN,
}

class Widget implements Sized {
    private final int[] xs = { 1, 2, 3 };

    Widget() {
        System.out.println("built");
    }

    public int size() {
        switch (xs.length) {
            case 0:
                return 0;
            default:
                return xs.length;
        }
    }
}
