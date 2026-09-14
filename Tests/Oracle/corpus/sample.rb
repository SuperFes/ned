# A representative class: methods, blocks, symbols, string interpolation.
require "json"

module Sample
  class Widget
    attr_accessor :name, :count

    def initialize(name, count = 0)
      @name = name
      @count = count
    end

    def to_s
      "#{@name} (#{@count})"
    end

    def self.from_hash(hash)
      new(hash[:name], hash[:count])
    end
  end
end

widgets = [1, 2, 3].map { |n| Sample::Widget.new("widget#{n}", n) }
widgets.each do |w|
  puts w.to_s if w.count > 1
end
