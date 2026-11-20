-- DESCRIPTION: Verilator: attributes of a record element
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
end entity t;

architecture sim of t is
  type t_rec is record
    mant : std_logic_vector(3 downto 0);
  end record;

  signal r   : t_rec := (mant => "1101");
  signal top : std_logic;
  signal low : std_logic_vector(2 downto 0);
  signal bits : integer range 0 to 15;
begin
  -- An attribute of a record element takes that element's own bounds
  top  <= r.mant(r.mant'left);
  low  <= r.mant(r.mant'left-1 downto 0);
  bits <= r.mant'length;

  check : process
  begin
    wait for 1 ns;
    if top = '1' then
      report "top is 1";
    else
      report "top is wrong";
    end if;
    if low = "101" then
      report "low is 101";
    else
      report "low is wrong";
    end if;
    if bits = 4 then
      report "bits is 4";
    else
      report "bits is wrong";
    end if;
    std.env.finish;
    wait;
  end process check;
end architecture sim;
