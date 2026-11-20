-- DESCRIPTION: Verilator: VHDL subtype range naming a constant that a function completes
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package wbits_pkg is
  function bits_of(value : natural) return natural;
  function tripled(value : natural) return natural;
end package wbits_pkg;

package body wbits_pkg is
  function bits_of(value : natural) return natural is
    variable w : natural := 1;
  begin
    for i in 0 to 15 loop
      if value / (2 ** i) > 0 then
        w := i + 1;
      end if;
    end loop;
    return w;
  end function bits_of;

  function tripled(value : natural) return natural is
  begin
    if value > 4 then
      return value + value;
    else
      return value * 3;
    end if;
  end function tripled;
end package body wbits_pkg;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.wbits_pkg.all;

entity t is
  port (
    i_delta : in  integer;
    o_len   : out integer;
    o_wrap  : out integer;
    o_wlen  : out integer;
    o_wide  : out integer;
    o_gen   : out std_logic_vector(1 downto 0)
  );
  -- The entity's own item, whose value only the body of TRIPLED gives
  constant wide_c : positive := tripled(3);
end entity t;

architecture rtl of t is
  -- Declared before the constant of the part that holds it is known to be 3
  constant bits_c : natural := bits_of(7);
  subtype small_t is unsigned(bits_c - 1 downto 0);
  subtype wide_t is unsigned(wide_c - 1 downto 0);
  signal src : unsigned(8 downto 0);
  signal wrapped : small_t;
  signal wide_item : wide_t;
begin
  src <= to_unsigned(9, 9) + i_delta;
  o_len <= small_t'length;
  wrapped <= resize(src, bits_c);
  o_wrap <= to_integer(wrapped);
  o_wlen <= wide_t'length;
  wide_item <= to_unsigned(61, wide_c);
  o_wide <= to_integer(wide_item);

  -- The subtype is first used in a statement, after the architecture's own items are done
  gen : for idx in 0 to 1 generate
    signal item : small_t;
    signal grown : unsigned(7 downto 0);
  begin
    item <= resize(src, bits_c);
    grown <= resize(item, 8);
    o_gen(idx) <= grown(3);
  end generate gen;
end architecture rtl;
