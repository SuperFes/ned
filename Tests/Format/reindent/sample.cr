require "json"

module Shapes
  abstract class Shape
    abstract def area : Float64
  end

  class Rect < Shape
    getter w : Float64
    getter h : Float64

    def initialize(@w, @h)
    end

    def area : Float64
      if w < 0
        raise ArgumentError.new("negative")
      elsif w == 0
        0.0
      else
        w * h
      end
    end

    def label
      case w
      when 0
        "empty"
      when 1, 2
        "thin"
      else
        "wide"
      end
    end
  end

  struct Point
    property x : Int32
  end

  enum Color
    Red
    Green
  end

  def self.total(shapes)
    sum = 0.0
    shapes.each do |s|
      sum += s.area
    end
    doubled = shapes.map { |s|
      s.area * 2
    }
    while sum > 100
      sum -= 1
    end
    sum
  rescue ex : ArgumentError
    puts ex.message
    0.0
  ensure
    puts "done"
  end
end
