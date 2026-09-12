namespace Demo
{
    enum Colour
    {
        Red,
        Green,
    }

    class Widget
    {
        private int[] xs = new int[] { 1, 2, 3 };

        public int Size
        {
            get { return xs.Length; }
        }

        public int Total()
        {
            var sum = 0;
            foreach (var x in xs)
            {
                sum += x;
            }
            return sum;
        }
    }
}
