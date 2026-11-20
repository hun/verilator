-- DESCRIPTION: Verilator: VHDL ports that cannot be modelled
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

-- An unconstrained port on the selected top has no width
entity top_in is
  port (
    i : in std_logic_vector
  );
end;

architecture rtl of top_in is
begin
end;
