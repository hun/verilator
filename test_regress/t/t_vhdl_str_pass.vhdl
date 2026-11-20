-- DESCRIPTION: Verilator: VHDL string generic passed on to a sub-instance
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity leaf is
  generic (
    val : string
  );
  port (
    len_o : out integer;
    eq_o  : out std_logic
  );
end entity leaf;

architecture rtl of leaf is
begin
  -- The length of the value this instance was given, and whether it is "abc"
  len_o <= val'length;
  g1 : if val = "abc" generate
    eq_o <= '1';
  end generate;
  g0 : if val /= "abc" generate
    eq_o <= '0';
  end generate;
end architecture rtl;

library ieee;
use ieee.std_logic_1164.all;

entity mid is
  generic (
    val : string
  );
  port (
    len_o : out integer;
    eq_o  : out std_logic
  );
end entity mid;

architecture rtl of mid is
begin
  -- The actual is this entity's own generic rather than a literal
  u : entity work.leaf
    generic map (val => val)
    port map (len_o => len_o, eq_o => eq_o);
end architecture rtl;

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    len0 : out integer;
    eq0  : out std_logic;
    len1 : out integer;
    eq1  : out std_logic
  );
end entity t;

architecture rtl of t is
begin
  u0 : entity work.mid
    generic map (val => "abc")
    port map (len_o => len0, eq_o => eq0);

  -- A different length and content, so a value that is not passed on shows
  u1 : entity work.mid
    generic map (val => "xyzw")
    port map (len_o => len1, eq_o => eq1);
end architecture rtl;
