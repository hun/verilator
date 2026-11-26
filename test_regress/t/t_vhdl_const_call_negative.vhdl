-- DESCRIPTION: Verilator: a negative constant folded from a function call
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (neg : out std_logic;
        q   : out std_logic_vector(7 downto 0));
end entity t;

architecture rtl of t is
  function pick (c : boolean; x : integer; y : integer) return integer is
  begin
    if c then
      return x;
    else
      return y;
    end if;
  end function pick;

  constant k : integer := pick(false, 32, 5) - 10;
begin
  neg <= '1' when k < 0 else '0';
  q <= std_logic_vector(to_signed(k, 8));
end architecture rtl;
