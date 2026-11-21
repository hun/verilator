-- DESCRIPTION: Verilator: VHDL an instantiation whose entity name is selected
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- An instantiation may name its entity with a selected name, `work.pkg.child`, as
-- poc's and bit-serial's cores do. The library is not modelled, so only the final
-- name is kept. Every value below is the one GHDL reports.

library ieee;
use ieee.std_logic_1164.all;

package subpkg is
  component subent is
    generic (
      WIDTH : natural := 4
    );
    port (
      d : in  std_logic_vector(WIDTH - 1 downto 0);
      q : out std_logic_vector(WIDTH - 1 downto 0)
    );
  end component;
end package;

library ieee;
use ieee.std_logic_1164.all;
use work.subpkg.all;

entity subent is
  generic (
    WIDTH : natural := 4
  );
  port (
    d : in  std_logic_vector(WIDTH - 1 downto 0);
    q : out std_logic_vector(WIDTH - 1 downto 0)
  );
end entity;

architecture rtl of subent is
begin
  q <= d;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use work.subpkg.all;

entity t_vhdl_selected_inst is
  port (
    d : in  std_logic_vector(3 downto 0);
    q : out std_logic_vector(3 downto 0)
  );
end entity;

architecture rtl of t_vhdl_selected_inst is
begin
  -- The entity is named through the package it is declared in
  u_sub : work.subpkg.subent
    generic map (WIDTH => 4)
    port map (d => d, q => q);
end architecture;
