-- DESCRIPTION: Verilator: a element of a signal in a sensitivity list
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A sensitivity item may be a subelement of a signal (IEEE 1076-2008 10.2):
-- the process runs when that element changes and not when another element of
-- the same signal does. The variable counts activations, so a widened
-- sensitivity would be visible.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package sens_pkg is
  type rec_t is record
    a : std_logic_vector(3 downto 0);
    b : std_logic;
  end record;
end package;

library ieee;
use ieee.std_logic_1164.all;
use work.sens_pkg.all;

entity t is
  port (
    rec_a : in  std_logic_vector(3 downto 0);
    rec_b : in  std_logic;
    v     : in  std_logic_vector(7 downto 0);
    n     : out std_logic_vector(7 downto 0);
    y     : out std_logic_vector(3 downto 0)
  );
end entity;

architecture rtl of t is
  signal rec : rec_t;
  signal count : std_logic_vector(7 downto 0);
begin
  rec.a <= rec_a;
  rec.b <= rec_b;

  process (rec.a, v(0))
    variable activations : std_logic_vector(7 downto 0) := (others => '0');
  begin
    activations := std_logic_vector(unsigned(activations) + 1);
    count <= activations;
    y <= rec.a xor (3 downto 0 => v(0));
  end process;

  n <= count;
end architecture;
