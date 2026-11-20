-- DESCRIPTION: Verilator: an unconstrained vector constant and generic take their width
--              from the literal that defaults them
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  generic (
    -- The mark leaves the width open; the literal fixes it at 5
    C_pixclk_div : std_logic_vector := "00111"
  );
  port (
    o : out std_logic_vector(4 downto 0)
  );
end entity t;

architecture rtl of t is
  constant c_cmd : std_logic_vector := "00101";
begin
  o <= C_pixclk_div and c_cmd;
end architecture rtl;