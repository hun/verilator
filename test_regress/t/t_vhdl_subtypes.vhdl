-- DESCRIPTION: Verilator: VHDL user-defined and local subtypes/array types
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

package types_pkg is
  subtype slv4 is std_logic_vector(3 downto 0);
  subtype small is integer range 0 to 7;
  type word_t is array (3 downto 0) of std_logic;
end;

library ieee;
use ieee.std_logic_1164.all;
use work.types_pkg.all;

entity t is
  port (
    a : in slv4;
    b : in word_t;
    n : in small;
    y : out slv4;
    z : out std_logic_vector(3 downto 0);
    g : out std_logic_vector(3 downto 0);
    u : out std_logic_vector(3 downto 0)
  );
end;

architecture rtl of t is
  -- Architecture-local type declarations resolve at their use sites
  subtype local_slv is std_logic_vector(3 downto 0);
  type local_arr is array (3 downto 0) of std_logic;
  signal s : local_slv;
  signal w : local_arr;
begin
  s <= a;
  w <= local_arr(a);
  y <= s;
  u <= std_logic_vector(w);

  process (a) is
    subtype proc_slv is std_logic_vector(3 downto 0);
    variable v : proc_slv;
  begin
    v := a;
    z <= v;
  end process;

  gen : for i in 0 to 0 generate
    subtype gen_slv is std_logic_vector(3 downto 0);
    signal gs : gen_slv;
  begin
    gs <= a;
    g <= gs;
  end generate;
end;
