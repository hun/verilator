-- DESCRIPTION: Verilator: string actuals in an overloaded call, and a string constant as
-- a generic actual
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- A `string` formal must accept a string actual: open-logic's choose() is called with
-- plain strings and with integer arguments, and the overload picks the right one. A
-- string *constant* can also be bound to a string generic, as the packet FIFO passes its
-- small-RAM style along.

library ieee;
use ieee.std_logic_1164.all;

package pk is
  function choose (s : boolean; t : string; f : string) return string;
  function choose (s : boolean; t : integer; f : integer) return integer;
end package;

package body pk is
  function choose (s : boolean; t : string; f : string) return string is
  begin
    if s then
      return t;
    else
      return f;
    end if;
  end function;

  function choose (s : boolean; t : integer; f : integer) return integer is
  begin
    if s then
      return t;
    else
      return f;
    end if;
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use work.pk.all;

entity style_blk is
  generic (Mode : string := "X"; Width : natural := 2);
  port (n : out natural);
end entity;

architecture rtl of style_blk is
begin
  n <= Mode'length + Width;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use work.pk.all;

entity t is
  generic (A : string := "x"; B : string := "same");
  port (
    from_overload : out natural;
    from_integer : out integer;
    from_constant : out natural
  );
end entity;

architecture rtl of t is
  constant Mode_c : string := "hello";
  constant C : string := choose(A = B, A, B);
  constant E : string := choose(false, "aa", "bbb");
begin
  from_overload <= C'length + E'length;
  from_integer <= choose(true, 3, 4);

  u : entity work.style_blk
    generic map (Mode => Mode_c, Width => 3)
    port map (n => from_constant);
end architecture;
