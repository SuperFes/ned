# A representative config: blocks, attributes, expressions, interpolation.
variable "region" {
  type    = string
  default = "us-east-1"
}

resource "aws_instance" "sample" {
  ami           = "ami-0abcdef1234567890"
  instance_type = var.small ? "t3.micro" : "t3.small"
  count         = 2

  tags = {
    Name = "sample-${var.region}"
  }
}

locals {
  full_name = "${var.region}-sample"
}
