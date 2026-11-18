entity t is
  port (y : out bit);
end;
architecture rtl of t is
  type op is (add, sub);
  signal s : op;
begin
  s <= op'val(5);
  y <= '0';
end;
