-- DESCRIPTION: Verilator: a VHDL variable hides an enumeration literal of a used package
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

package codes_pkg is
  type code_t is (ra, rb);
end package codes_pkg;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
library work;
use work.codes_pkg.all;

entity t is
  port (
    o : out std_logic_vector(7 downto 0)
  );
end entity t;

architecture rtl of t is
begin
  process
    -- `ra` is also a literal of the used package, which this variable hides
    variable ra : std_logic_vector(7 downto 0);
  begin
    ra := std_logic_vector(to_unsigned(5, 8));
    o <= ra;
    wait;
  end process;
end architecture rtl;
