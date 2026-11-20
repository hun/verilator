-- DESCRIPTION: Verilator: VHDL for generate unrolling
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

package gen_pkg is
  generic (N : integer);
end package;

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a : in std_logic_vector(3 downto 0);
    y : out std_logic_vector(3 downto 0)
  );
end;

architecture rtl of t is
  package inst_pkg is new work.gen_pkg generic map (N => 3);
begin
  blk : block
    signal tmp : std_logic_vector(3 downto 0);
  begin
    tmp <= a;
    g : for i in 0 to 3 generate
      y(i) <= tmp(i);
    end generate;
  end block;
end;
